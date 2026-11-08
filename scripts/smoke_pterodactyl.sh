#!/bin/bash
# Run against a built image; approximate Wings' container restrictions.
set -euo pipefail
IMAGE="${1:?image:tag required}"
EGG="${2:?rendered egg path required}"
VOLUME=$(docker volume create)
CID=""
cleanup() {
    if [ -n "$CID" ]; then
        docker logs "$CID" || true
        docker rm -fv "$CID" >/dev/null || true
    fi
    docker volume rm "$VOLUME" >/dev/null
}
trap cleanup EXIT

# Wings owns and chowns its bind mount; deliberately use a different UID from the image.
docker run --rm --user 0:0 --entrypoint sh -v "$VOLUME:/home/container" "$IMAGE" -ec 'chown -R 12345:12345 /home/container'
STARTUP=$(jq -er '.startup' "$EGG")
CID=$(docker run -d --init --read-only --cap-drop ALL --security-opt no-new-privileges --tmpfs /tmp:rw,exec,nosuid,size=128m --user 12345:12345 -v "$VOLUME:/home/container" -e STARTUP="$STARTUP" -e SERVER_PORT=29015 -p 127.0.0.1::29015/tcp -p 127.0.0.1::29015/udp "$IMAGE")

wait_ready() {
    local port
    port=$(docker port "$CID" 29015/tcp | head -1 | cut -d: -f2)
    for attempt in $(seq 1 30); do
        # The status endpoint answers on the game port from the moment the
        # listener is up, which is before the start has finished, so the
        # started line is waited for in the same loop rather than asserted
        # once the endpoint replies. Not grep -q: it exits on the first match,
        # and under pipefail the SIGPIPE docker logs then takes fails the step
        # once the log holds a second start.
        if curl --max-time 2 -fsS "http://127.0.0.1:${port}/" | jq -e '.port == 29015 and .mod_name == "KCDC"' >/dev/null && docker logs "$CID" 2>&1 | grep 'KCDC Server successfully started' >/dev/null; then
            return 0
        fi
        sleep 2
    done
    echo "Server did not become ready under Wings restrictions" >&2
    return 1
}

wait_ready
# No package key file: since Framework 40.2 the resource package key is
# derived from the mod slug and secret and is never stored.
docker exec "$CID" sh -ec 'test "$(id -u)" = 12345; test -s server.json'
docker exec "$CID" cat /proc/net/udp | grep -E ':7157 '

# The gamemode was seeded, under the read-only root and foreign UID Wings
# imposes -- which is where a seeding copy is likeliest to fail. It ships as
# source, so what is proven here is the seeding, not that the server ran it.
docker exec "$CID" test -f /home/container/resources/kcdc-gamemode/src/server/index.ts

# Marked as an operator would; the restart below proves seeding happens once.
docker exec "$CID" sh -ec 'printf "// operator" >> /home/container/resources/kcdc-gamemode/src/server/index.ts'
EDITED=$(docker exec "$CID" sha256sum /home/container/resources/kcdc-gamemode/src/server/index.ts)

# The egg's ^C stop setting maps to SIGINT.
docker kill --signal SIGINT "$CID" >/dev/null
timeout 30 docker wait "$CID"
test "$(docker inspect -f '{{.State.ExitCode}}' "$CID")" = 0

# Start the same server again and ensure the operator's edit survives the restart.
docker start "$CID" >/dev/null
wait_ready
test "$(docker exec "$CID" sha256sum /home/container/resources/kcdc-gamemode/src/server/index.ts)" = "$EDITED"
docker stop --time 30 "$CID" >/dev/null
test "$(docker inspect -f '{{.State.ExitCode}}' "$CID")" = 0
