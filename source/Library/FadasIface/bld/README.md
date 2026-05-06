# How to build FadasIface skel library

1, setup hexagon 6.2 sdk
```sh
source $(HEXAGON_SDK_PATH)/hexagon-sdk-6.2.0/setup_sdk_env.source
```
2, copy fadas library and header files to bld path
```sh
cd $(QCNODE_PATH)/source/Library/FadasIface/bld
mkdir include
cp $(BSP_ROOT)/AMSS/multimedia/fadas/fadas/inc/* ./include/
cp $(BSP_ROOT)/prebuilt/aarch64le/lib/fadas/lib/qurt/nsp/v68/libfadasNsp.so ./
```
3, copy crc source and header files to bld path
```sh
cp $(BSP_ROOT)/AMSS/safety/safetylibs/crc32/public/amss/crc32.h ./include/
cp $(BSP_ROOT)/AMSS/safety/safetylibs/public/amss/crc.h ./include/
cp $(BSP_ROOT)/AMSS/safety/safetylibs/public/amss/safetylibs_types.h ./include/
cp $(BSP_ROOT)/AMSS/safety/safetylibs/crc32/src/crc32.c ./
```
4, build skel library according to hexagon version
```sh
make tree V=hexagon_Release_toolv19_v81 VERBOSE=1 V_dynamic=1 # for 8797
make tree V=hexagon_Release_toolv88_v75 VERBOSE=1 V_dynamic=1 # for 8775
make tree V=hexagon_Release_toolv88_v73 VERBOSE=1 V_dynamic=1 # for 8650
make tree V=hexagon_Release_toolv88_v68 VERBOSE=1 V_dynamic=1 # for 8295
```

5, copy source files and skel library
```sh
cp -fv hexagon_Release_toolv*_v*/ship/libFadasIface_skel.so ../prebuilt/dsp
cp -fv hexagon_Release_toolv*_v*/FadasIface.h ../FadasIface.h
cp -fv hexagon_Release_toolv*_v*/FadasIface_stub.c ../FadasIface.c
$(HEXAGON_SDK_ROOT)/tools/HEXAGON_Tools/*/Tools/bin/hexagon-strip ../prebuilt/dsp/libFadasIface_skel.so 
```

6, add signature
```sh
python3 $(SWIV_PATH)/swiv_build_utility.py -i ../prebuilt/dsp/libFadasIface_skel.so -o ../prebuilt/dsp/libFadasIface_skel.so
```