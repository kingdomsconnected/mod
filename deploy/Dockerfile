# Runtime only. Prepare the allowlisted context with scripts/stage_server_image.py.
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates libcurl4t64 libstdc++6 libssl3t64 zlib1g \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 container \
    && useradd --uid 10001 --gid 10001 --create-home --home-dir /home/container --shell /bin/bash container \
    && mkdir -p /home/container/resources /home/container/logs /home/container/.packages \
    && chown -R 10001:10001 /home/container

COPY --chmod=0555 runtime/KCDCServer runtime/crashpad_handler /opt/kcdc/
COPY --chmod=0444 runtime/libnode.so* /opt/kcdc/
COPY --chmod=0444 runtime/LICENSE.txt runtime/NOTICE.txt /opt/kcdc/
COPY --chmod=0444 runtime/licenses/Framework-LICENSE.txt /opt/kcdc/licenses/
COPY --chmod=0555 entrypoint.sh /usr/local/bin/kcdc-entrypoint
COPY --chmod=0444 egg-kcdc.json /usr/share/kcdc/egg-kcdc.json

# The default gamemode. Every `/` command lives in a resource, so a container
# with an empty resources volume answers none of them; the entrypoint seeds
# this into the volume once.
#
# Not `COPY --chmod=0444`: that mode lands on the directories as well, and a
# directory without its execute bit cannot be entered, so nothing -- not the
# entrypoint's seeding, not root -- can read the tree it just copied.
COPY default-resources/ /opt/kcdc/default-resources/
RUN chmod -R a=rX /opt/kcdc/default-resources

# Resolve streamed resources into the persistent directory mounted by Wings or Docker.
RUN ln -s /home/container/resources /opt/kcdc/resources && ln -s /home/container/.packages /opt/kcdc/.packages

ENV LD_LIBRARY_PATH=/opt/kcdc USER=container HOME=/home/container
WORKDIR /home/container
USER container
EXPOSE 27015/udp 27015/tcp
VOLUME ["/home/container"]
STOPSIGNAL SIGTERM
ENTRYPOINT ["/usr/local/bin/kcdc-entrypoint"]
