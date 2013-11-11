export PATH=/opt/toolchains/arm-2010q1/bin:$PATH
export LD_LIBRARY_PATH=/opt/toolchains/arm-2010q1/arm-none-linux-gnueabi/bin:$LD_LIBRARY_PATH
make arch=arm android_t1_chn_cmcc_omap4430_r07_user_defconfig
make
