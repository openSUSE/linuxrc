FROM registry.opensuse.org/yast/head/containers/yast-cpp:latest

RUN zypper --non-interactive in --no-recommends \
  e2fsprogs-devel \
  libblkid-devel \
  libcurl-devel \
  readline-devel \
  libmediacheck-devel \
  tmux

COPY . /usr/src/app
