#include "comm.h"
#include "ride.h"
#include "theme.h"

// Buffers. Telemetry is ten small tuples, so a couple of hundred bytes is
// generous; asking for app_message_inbox_size_maximum() would reserve several
// kilobytes of the app's heap for no gain, and heap is the scarce resource on
// the 144x168 platforms.
#define INBOX_SIZE   256
#define OUTBOX_SIZE  128

// How long after a local button press we decline to believe the phone's view
// of the state. Without this there is a race on stop: the watch sends STOP and
// goes idle, but a telemetry packet already in flight still says RECORDING and
// would drag the screen back into a ride that has ended.
#define LOCAL_AUTHORITY_S  3

// How long to wait before re-attempting a command the radio would not take,
// and how many times. Three attempts over a second and a half covers the
// window where Bluetooth is still coming up at launch without leaving a
// button press feeling unacknowledged.
#define RETRY_DELAY_MS  500
#define RETRY_MAX       3

// STOP is not best-effort like the others. Every other command is corrected by
// the next telemetry packet -- if a PAUSE is lost, the phone says RECORDING a
// second later and the watch simply agrees. A lost STOP has no such corrector:
// the phone carries on recording a ride the rider has already been shown a
// summary for, and the watch would then adopt that RECORDING state and slide
// back into a ride it had finished.
//
// So STOP repeats on this interval until the phone confirms STATE_IDLE, and
// while it is outstanding the watch refuses to believe any state but idle.
#define STOP_REPEAT_MS  2000

static CommUpdateHandler s_handler;
static uint8_t   s_retry_cmd;
static uint32_t  s_retry_moving;
static uint8_t   s_retries_left;
static AppTimer *s_retry_timer;
static time_t    s_local_change_at;

// The phone's id for the ride in progress, learned from telemetry and echoed
// back on every command so neither side can act on a stale idea of which ride
// is current. Cached here rather than read from the model at send time,
// because ride_local_stop() clears the model before CMD_STOP is sent.
static uint32_t  s_ride_id;

static bool      s_stop_pending;
static uint32_t  s_stop_ride_id;
static uint32_t  s_stop_moving;
static AppTimer *s_stop_timer;

// Read an unsigned value without assuming the width the sender picked.
//
// The union in Tuple is only valid at the field matching `.length` -- the
// struct is packed and `value[]` is a flexible array, so reading `.uint32`
// from a one-byte tuple reads three bytes into the *next* tuple's key. The
// Android side is free to encode KEY_T_FIX (values 0-3) as a single byte, and
// nothing in the protocol version check would catch the disagreement, because
// both sides agree on the key and differ only on its width.
static uint32_t tuple_uint(const Tuple *t) {
  if (t->type != TUPLE_UINT && t->type != TUPLE_INT) {
    return 0;   // a string or byte array on a numeric key: ignore, don't guess
  }
  switch (t->length) {
    case 1:  return t->value->uint8;
    case 2:  return t->value->uint16;
    case 4:  return t->value->uint32;
    default: return 0;
  }
}

// Returns false when the radio would not even accept the message, which is
// distinct from it being accepted and later failing -- the latter arrives at
// outbox_failed, the former is only visible here.
static bool send_cmd_with_id(uint8_t cmd, uint32_t moving, uint32_t ride_id) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) {
    return false;
  }
  if (dict_write_uint8(iter, KEY_C_CMD, cmd) != DICT_OK) {
    return false;
  }
  // START has no id to quote -- the phone is about to mint one -- and SYNC is
  // a question about whatever ride exists, so neither carries it.
  if (cmd != CMD_START && cmd != CMD_SYNC &&
      dict_write_uint32(iter, KEY_C_RIDE_ID, ride_id) != DICT_OK) {
    return false;
  }
  if (cmd == CMD_STOP &&
      dict_write_uint32(iter, KEY_C_MOVING, moving) != DICT_OK) {
    return false;
  }
  dict_write_end(iter);
  return app_message_outbox_send() == APP_MSG_OK;
}

static bool send_now(uint8_t cmd, uint32_t moving) {
  return send_cmd_with_id(cmd, moving, s_ride_id);
}

static void retry_timer_fired(void *context);

// Try once, and arm a timer if that attempt never made it onto the radio.
static void attempt_send(void) {
  if (send_now(s_retry_cmd, s_retry_moving)) {
    return;   // in flight; outbox_sent or outbox_failed decides what happens next
  }
  if (s_retries_left > 0) {
    s_retries_left--;
    s_retry_timer = app_timer_register(RETRY_DELAY_MS, retry_timer_fired, NULL);
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "giving up on cmd %u", (unsigned)s_retry_cmd);
  }
}

static void retry_timer_fired(void *context) {
  s_retry_timer = NULL;
  attempt_send();
}

// ---------------------------------------------------------------------------
// STOP, which repeats until acknowledged
// ---------------------------------------------------------------------------

static void stop_timer_fired(void *context);

static void arm_stop_repeat(void) {
  if (s_stop_timer) {
    app_timer_cancel(s_stop_timer);
  }
  s_stop_timer = app_timer_register(STOP_REPEAT_MS, stop_timer_fired, NULL);
}

static void stop_timer_fired(void *context) {
  s_stop_timer = NULL;
  if (!s_stop_pending) {
    return;
  }
  // Unbounded by design. There is no sensible number of attempts after which
  // leaving the phone recording forever becomes acceptable; the loop ends when
  // the phone says idle, or when the watchapp exits.
  send_cmd_with_id(CMD_STOP, s_stop_moving, s_stop_ride_id);
  arm_stop_repeat();
}

static void clear_stop_pending(void) {
  s_stop_pending = false;
  if (s_stop_timer) {
    app_timer_cancel(s_stop_timer);
    s_stop_timer = NULL;
  }
}

// ---------------------------------------------------------------------------
// Inbound
// ---------------------------------------------------------------------------

static void inbox_received(DictionaryIterator *iter, void *context) {
  RideState *r = ride_get();

  uint8_t prev_state    = r->state;
  bool    have_state    = false;
  uint8_t phone_state   = STATE_IDLE;
  bool    ended_by_phone = false;

  for (Tuple *t = dict_read_first(iter); t; t = dict_read_next(iter)) {
    switch (t->key) {
      case KEY_T_DISTANCE:  r->distance_m    = tuple_uint(t);           break;
      case KEY_T_SPEED:     r->speed_cms     = (uint16_t)tuple_uint(t); break;
      case KEY_T_ASCENT:    r->ascent_m      = (uint16_t)tuple_uint(t); break;
      case KEY_T_MAXSPEED:  r->max_speed_cms = (uint16_t)tuple_uint(t); break;
      case KEY_T_MOVING:    r->moving_s      = tuple_uint(t);           break;
      case KEY_T_START:     r->start_time    = tuple_uint(t);           break;
      case KEY_T_FIX:       r->fix           = (uint8_t)tuple_uint(t);  break;
      case KEY_T_PHONEBATT: r->phone_batt    = (uint8_t)tuple_uint(t);  break;

      case KEY_T_UNITS:
        ride_set_units((uint8_t)tuple_uint(t));
        break;

      case KEY_T_COL_BG:
        theme_set_bg(tuple_uint(t));
        break;

      case KEY_T_COL_ACCENT:
        theme_set_accent(tuple_uint(t));
        break;

      case KEY_T_RIDE_ID:
        r->ride_id = tuple_uint(t);
        s_ride_id  = r->ride_id;
        break;

      case KEY_T_VERSION:
        // Stored, not just logged: the ready screen shows a mismatch, which is
        // the whole point of sending a version at all. A log line nobody reads
        // would leave a stale pairing failing silently.
        r->phone_version = (uint8_t)tuple_uint(t);
        break;

      case KEY_T_STATE:
        have_state  = true;
        phone_state = (uint8_t)tuple_uint(t);
        break;

      default:
        break;
    }
  }

  ride_telemetry_received();

  // The phone's state is authoritative except in the moments right after a
  // local press, when a packet composed before our command reached the phone
  // may still be describing the old world.
  bool local_authority =
      (s_local_change_at != 0) &&
      (time(NULL) - s_local_change_at < LOCAL_AUTHORITY_S);

  // A STOP we are still chasing overrides everything: until the phone confirms
  // idle, the only state we will accept from it is idle. Without this the very
  // packet that proves STOP has not landed yet -- one still saying RECORDING --
  // would drag the model back into the ride we are trying to end.
  if (s_stop_pending) {
    if (have_state && phone_state == STATE_IDLE) {
      clear_stop_pending();
    }
    have_state = false;
  }

  if (have_state && !local_authority) {
    if (phone_state == STATE_IDLE && prev_state != STATE_IDLE) {
      // Stop was pressed in the Android app. Leave the model alone: the UI
      // handler runs ride_local_stop(), which is what snapshots the ride into
      // the on-watch history. Adopting IDLE here would zero the numbers first
      // and the saved summary would be empty.
      ended_by_phone = true;
    } else if (phone_state != STATE_IDLE) {
      r->state = phone_state;
    }
  }

  if (s_handler) {
    s_handler(ended_by_phone);
  }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "inbox dropped: %d", (int)reason);
}

// ---------------------------------------------------------------------------
// Outbound
// ---------------------------------------------------------------------------

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason,
                          void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "outbox failed: %d", (int)reason);
  if (s_retries_left > 0) {
    s_retries_left--;
    attempt_send();
  }
}

static void outbox_sent(DictionaryIterator *iter, void *context) {
  s_retries_left = 0;   // delivered; stop trying
}

void comm_send_cmd(uint8_t cmd, uint32_t moving) {
  // Every local transition goes through here, so this is the natural place to
  // start the window during which we trust ourselves over the phone.
  s_local_change_at = time(NULL);

  // A newer command supersedes whatever was still being retried: a queued
  // START that lands after the rider has already pressed STOP would restart a
  // finished ride. Cancel the old attempt outright.
  if (s_retry_timer) {
    app_timer_cancel(s_retry_timer);
    s_retry_timer = NULL;
  }

  if (cmd == CMD_STOP) {
    // Latch it. s_ride_id still holds the ride being stopped at this point --
    // the model has already been cleared by ride_local_stop(), which is why
    // comm keeps its own copy.
    s_stop_pending = true;
    s_stop_ride_id = s_ride_id;
    s_stop_moving  = moving;
    s_ride_id      = 0;
    arm_stop_repeat();
  } else if (cmd == CMD_START) {
    // Starting a new ride abandons any outstanding stop: the phone has plainly
    // moved on, and repeating a STOP for the previous ride could only end the
    // new one if ids ever collided.
    clear_stop_pending();
  }

  s_retry_cmd    = cmd;
  s_retry_moving = moving;
  s_retries_left = RETRY_MAX;
  attempt_send();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void comm_init(CommUpdateHandler handler) {
  s_handler = handler;

  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  app_message_register_outbox_sent(outbox_sent);

  AppMessageResult open = app_message_open(INBOX_SIZE, OUTBOX_SIZE);
  if (open != APP_MSG_OK) {
    // Nothing will ever arrive or leave. Worth a loud log: the symptom on the
    // watch is a permanently unlinked-looking app with no other explanation.
    APP_LOG(APP_LOG_LEVEL_ERROR, "app_message_open failed: %d", (int)open);
  }

  // Ask the phone where things stand. If a ride is already running -- the app
  // was closed and reopened mid-climb -- this is what puts the numbers back on
  // screen instead of showing a fresh zeroed ride.
  comm_send_cmd(CMD_SYNC, 0);
  s_local_change_at = 0;   // SYNC changes nothing locally, so claim no authority
}

void comm_deinit(void) {
  if (s_retry_timer) {
    app_timer_cancel(s_retry_timer);
    s_retry_timer = NULL;
  }
  // Note what is lost here: if the watchapp is closed with a STOP still
  // unacknowledged, nothing on the watch will chase it again. The phone is
  // expected to end a ride whose watch has gone silent on its own terms --
  // it owns the recording, and it is the side still running.
  clear_stop_pending();
  s_retries_left = 0;
  s_local_change_at = 0;
  s_ride_id = 0;
  s_handler = NULL;
  app_message_deregister_callbacks();
}
