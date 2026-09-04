#!/bin/bash

DOCKER_TAG=qcnode-toolchain-base:v2.0
docker build --build-arg DOCKER_REGISTRY=$DOCKER_REGISTRY -t $DOCKER_TAG .
