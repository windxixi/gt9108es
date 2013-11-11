ifeq ($(CONFIG_BATTERY_MONITOR_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif

obj-$(CONFIG_BATTERY_MONITOR)		+= battery_monitor.o sleep_i2c1.o sleep_i2c4.o sleep_madc.o 
obj-$(CONFIG_BATTERY_CHARGER_MAX14577)	+= charger_max14577.o
obj-$(CONFIG_FUEL_GAUGE_MAX17043)	+= fuelgauge_max17043.o










