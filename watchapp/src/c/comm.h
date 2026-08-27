// AppMessage transport. The only file that touches the Pebble messaging API.

#pragma once

#include <pebble.h>
#include "protocol.h"

// Raised after an inbound packet has been folded into the ride model.
// `phone_ended_ride` is true when the phone reported STATE_IDLE while the
// watch still thought a ride was live -- i.e. the rider pressed stop in the
// Android app. The UI uses it to jump to the summary screen, which is what
// makes the phone a genuine second head for the same ride rather than a
// sensor the watch merely reads.
typedef void (*CommUpdateHandler)(bool phone_ended_ride);

void comm_init(CommUpdateHandler handler);
void comm_deinit(void);

// Fire a command at the phone. `moving` is only attached to CMD_STOP, where
// the phone cross-checks it against its own figure. Pass 0 for every other
// command.
//
// One retry is attempted on failure; beyond that the command is dropped rather
// than queued. A stale START arriving thirty seconds late would be worse than
// no START at all, and the phone re-states the truth every second anyway.
void comm_send_cmd(uint8_t cmd, uint32_t moving);
