# PlatformIO pre-build script: fixes an endless loop in mobizt/FirebaseClient 2.2.13 (AsyncClient.h).
#
# In a synchronous call the read loop re-armed the read timeout before every pass, even when no byte arrived.
# If the connection stalled or closed after a part of the response (weak WiFi), the timeout could never
# expire: the network task spun forever and the task watchdog restarted the device ("task watchdog (hang)").
# Patched: the timer is only re-armed when data is available, so a stalled read fails after
# kFirebaseSyncTimeoutS and the connection is reopened.
#
# The library version is pinned in platformio.ini; if the code does not match any more, the build stops.
import os
import re

Import("env")  # noqa: F821  (provided by PlatformIO)

MARKER = "brewpass-patch: read timeout"

ORIGINAL = re.compile(
    r"(while \(sData->return_type == ret_continue && \(sData->response\.httpCode == 0 \|\| "
    r"sData->response\.respCtx\.stage != res_handler::response_stage_finished\)\)\s*\{(\r?\n)([ \t]*))"
    r"sData->response\.feedTimer\(!sData->async && sync_read_timeout_sec > 0 \? sync_read_timeout_sec : -1\);"
)


def patch(path):
    with open(path, "rb") as f:
        text = f.read().decode("utf-8")
    if MARKER in text:
        return
    def repl(m):
        nl, indent = m.group(2), m.group(3)
        return (m.group(1) + "// " + MARKER + ": only re-armed when data arrived (a stalled read used to loop forever)"
                + nl + indent + "if (sData->async || sData->response.tcpAvailable() > 0)"
                + nl + indent + "    sData->response.feedTimer(!sData->async && sync_read_timeout_sec > 0 ? sync_read_timeout_sec : -1);")
    new_text, n = ORIGINAL.subn(repl, text)
    if n != 1:
        print("patch_firebaseclient: the expected code was not found in %s - check the FirebaseClient version" % path)
        env.Exit(1)  # noqa: F821
    with open(path, "wb") as f:
        f.write(new_text.encode("utf-8"))
    print("patch_firebaseclient: read timeout fix applied")


libdeps = env.subst("$PROJECT_LIBDEPS_DIR/$PIOENV")  # noqa: F821
target = os.path.join(libdeps, "FirebaseClient", "src", "core", "AsyncClient", "AsyncClient.h")
if os.path.isfile(target):
    patch(target)
else:
    print("patch_firebaseclient: %s not found - FirebaseClient not installed for this environment?" % target)
    env.Exit(1)  # noqa: F821
