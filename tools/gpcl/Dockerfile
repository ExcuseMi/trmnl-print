# Renders our PCL to PNG for a look (no printer involved): builds Artifex's gpcl6 from the ghostpdl release.
FROM debian:12-slim
RUN apt-get update && apt-get install -y --no-install-recommends build-essential curl ca-certificates pkg-config && rm -rf /var/lib/apt/lists/*
RUN mkdir /src && cd /src && curl -sL https://github.com/ArtifexSoftware/ghostpdl-downloads/releases/download/gs10040/ghostpdl-10.04.0.tar.gz | tar xz --strip-components=1 \
 && ./configure --without-x --disable-cups --disable-dynamic >/dev/null && make -j4 pcl 2>&1 | tail -15
RUN find /src -name gpcl6 -type f | head -1 | xargs -I{} cp {} /usr/local/bin/gpcl6
ENTRYPOINT ["gpcl6"]
