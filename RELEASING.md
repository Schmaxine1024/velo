# Releasing

Releases are tag-driven. Push a tag, CI builds a signed APK and publishes a
GitHub Release with it attached — which is what [Obtainium][] polls.

[Obtainium]: https://github.com/ImranR98/Obtainium

## The thing that will bite you: signing

Android refuses to install an update signed with a different key than the one
already on the phone. There is no override short of uninstalling, which throws
away the ride database.

So the release APK **must** be signed with the same key every time. CI does
that from repository secrets; the key itself is never in this repo. The release
workflow refuses to publish if the secret is missing rather than quietly
falling back to a debug key, because that failure would not surface until
someone's phone rejected an update days later.

The keystore lives outside the repo at `~/claude/pebbles/velo-signing/`.
**Back it up somewhere durable.** Lose it and you cannot ship an update that
any existing install will accept.

Current signing identity:

```
CN=Velo, O=lianas.org, C=GB
SHA-256  D1:D6:EE:E1:0A:12:C9:A3:99:20:96:C8:10:8A:DD:14:2B:EA:81:EA:0F:AD:E9:C1:ED:90:FF:5E:5A:E5:7F:69
```

## One-time setup

Add four repository secrets under **Settings → Secrets and variables →
Actions → New repository secret**:

| Secret | Value |
|---|---|
| `VELO_KEYSTORE_BASE64` | contents of `~/claude/pebbles/velo-signing/keystore.base64` |
| `VELO_KEYSTORE_PASSWORD` | contents of `~/claude/pebbles/velo-signing/password.txt` |
| `VELO_KEY_PASSWORD` | same as `VELO_KEYSTORE_PASSWORD` |

The key alias is deliberately *not* a secret. It is `velo`, which is not
sensitive — and making it one taught GitHub to redact the string "velo" from
every log line in the repo, including filenames, which hid a build failure
behind a row of asterisks.

The base64 file is one long line with no trailing newline; paste it whole.

## Cutting a release

```sh
git tag v1.0.1
git push origin v1.0.1
```

That runs `.github/workflows/release.yml`, which:

1. derives `versionName` from the tag and `versionCode` arithmetically from it
   (`1.2.3` → `10203`), so the code always increases and never depends on a run
   counter that could reset;
2. builds and signs the APK;
3. verifies it is **not** debug-signed, failing loudly if it is;
4. builds the watchapp `.pbw` on a best-effort basis;
5. publishes the release with both attached.

You can also run it by hand from the Actions tab via *workflow_dispatch* if you
want a build without moving a tag.

## Why the pbw build is allowed to fail

The Pebble SDK is unmaintained and its toolchain comes from community mirrors.
`continue-on-error` keeps a mirror outage from blocking a phone release; the
release simply ships without the `.pbw` and you attach one built locally. The C
is still checked on every push by the `fmtcheck` job, which needs nothing but
gcc.

## Pointing Obtainium at it

Add an app in Obtainium with this repository's URL. It picks the `.apk` asset
out of each release automatically. The repo is public, so no token is needed.

Obtainium sees a new version when `versionCode` increases — which is why step 1
above matters. Re-tagging the same version number will not produce an update.
