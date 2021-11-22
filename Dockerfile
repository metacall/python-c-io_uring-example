#
#	MetaCall Python C io_uring Example by Parra Studios
#	An example of embedding io_uring (from C) into Python.
#
#	Copyright (C) 2016 - 2021 Vicente Eduardo Ferrer Garcia <vic798@gmail.com>
#
#	Licensed under the Apache License, Version 2.0 (the "License");
#	you may not use this file except in compliance with the License.
#	You may obtain a copy of the License at
#
#		http://www.apache.org/licenses/LICENSE-2.0
#
#	Unless required by applicable law or agreed to in writing, software
#	distributed under the License is distributed on an "AS IS" BASIS,
#	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#	See the License for the specific language governing permissions and
#	limitations under the License.
#

FROM debian:bookworm-slim

# Image descriptor
LABEL copyright.name="Vicente Eduardo Ferrer Garcia" \
	copyright.address="vic798@gmail.com" \
	maintainer.name="Vicente Eduardo Ferrer Garcia" \
	maintainer.address="vic798@gmail.com" \
	vendor="MetaCall Inc." \
	version="0.1"

# Install dependencies
RUN apt-get update \
	&& apt-get install -y --no-install-recommends \
		build-essential \
		cmake \
		libclang-12-dev \
		libffi-dev \
		liburing-dev \
		ca-certificates \
		git \
		python3-dev \
		python3-pip \
	&& pip3 install metacall

# Set working directory to root
WORKDIR /root

# Clone and build the project (TODO: --branch v0.5.11)
RUN git clone https://github.com/metacall/core \
	&& mkdir core/build && cd core/build \
	&& cmake \
		-DOPTION_BUILD_LOADERS_C=On \
		-DOPTION_BUILD_LOADERS_PY=On \
		-DOPTION_BUILD_PORTS=On \
		-DOPTION_BUILD_PORTS_PY=On \
		-DOPTION_BUILD_DETOURS=Off \
		-DOPTION_BUILD_SCRIPTS=Off \
		-DOPTION_BUILD_TESTS=Off \
		-DOPTION_BUILD_EXAMPLES=Off \
		.. \
	&& cmake --build . --target install \
	&& ldconfig /usr/local/lib \
	&& cd ../.. \
	&& rm -rf core

# Copy source files
COPY index.py /root/
COPY scripts/uring.c scripts/script.ld /home/scripts/

# Set up enviroment variables
ENV LOADER_LIBRARY_PATH=/usr/local/lib \
	LOADER_SCRIPT_PATH=/home/scripts

CMD [ "metacallcli", "/root/index.py" ]
