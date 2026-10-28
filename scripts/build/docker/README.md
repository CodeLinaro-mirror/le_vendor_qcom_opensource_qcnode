# Guide to build qcnode-toolchain base docker

## Notes
The qcnode-toolchain base docker provides a basic host toolchain environment that splits from platform SDK toolchains, users can directly map the platform SDK toolchains path to this docker with '-v' option, thus the switching of platform SDK toolchains version could be easily.

## How to build qcnode-toolchain-base docker

- Option 1: build qcnode-toolchain-base docker image from Dockerfile.
The qcnode-toolchain-base image is built based on ubuntu 24.04, user can build the image with this script:
```
  ./build-docker-image.sh
```

After building, the docker image could be shown as below:
```
  docker images -a

  IMAGE                        ID             DISK USAGE   CONTENT SIZE   EXTRA
  qcnode-toolchain-base:v2.0   9e47d9e50393       1.69GB             0B
```

- Option 2: Load pre-built qcnode-toolchain-base docker image
User can load pre-built image directly with this command:
```
  docker load -i qcnode-toolchain-base-24.04-v2.0.tar
```
