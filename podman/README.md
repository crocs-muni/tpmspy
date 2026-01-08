# Building Podman image

## Requirements

- `podman` or `docker`


## Instructions

In this tutorial, Podman is used as a safer alternative to Docker. However,
their Command-Line Interfaces are compatible, so you should get exactly the same
result if you replace `podman` with `docker`.

1. Build the image. In the directory with `Containerfile`, run

      podman build --tag tpmspy .

2. Run the image with `tpmspy` mounted. From the **parent** directory
   (one that contains `meson.build`, `src` etc.):

      podman run --rm -it -v "$PWD":/tpmspy tpmspy

3. You should get a shell where you can (mostly) follow the manual for TPMSpy:

      cd /tpmspy
      meson setup build
      ninja -C build
