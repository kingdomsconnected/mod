#!/bin/bash
set -euo pipefail

# The Framework looks for crashpad_handler in the working directory.
cd /home/container
mkdir -p resources logs .packages
ln -sf /opt/kcdc/crashpad_handler crashpad_handler

# Seed each default resource into the operator's volume when it is not already
# there, so a fresh container answers commands. An existing directory is left
# alone, edited or emptied.
#
# A seed that cannot be planted -- read-only mount, foreign UID -- is logged and
# stepped over: a server without its gamemode beats one that will not start.
for default in /opt/kcdc/default-resources/*/; do
    # An unmatched glob comes through as the pattern itself.
    if [ ! -d "$default" ]; then
        continue
    fi
    name=$(basename "$default")
    if [ -e "resources/$name" ]; then
        continue
    fi
    if cp -R "$default" "resources/$name" && chmod -R u+w "resources/$name"; then
        echo "kcdc-entrypoint: seeded default resource '$name'"
    else
        rm -rf "resources/$name" 2>/dev/null || true
        echo "kcdc-entrypoint: could not seed default resource '$name' into $PWD/resources; the server will start without it" >&2
    fi
done

# Explicit Docker arguments take precedence. Wings supplies STARTUP instead.
# Expand egg placeholders once; do not log commands that may contain secrets.
if [ "$#" -eq 0 ] && [ -n "${STARTUP:-}" ]; then
    STARTUP_COMMAND=$(printf '%s' "$STARTUP" | sed -e 's/{{/${/g' -e 's/}}/}/g')
    exec /bin/bash -eu -c "exec ${STARTUP_COMMAND}"
fi
exec /opt/kcdc/KCDCServer "$@"
