# waterforce_x240_poc
Waterforce x240 kernel Module proof of concept code
Modified code from https://github.com/aleksamagicka/waterforce-hwmon to add active CPU temp. feeding to the waterforce
RGB color based on the fan speed pump speed and cpu temperature

# to build
make

# to install 
make dev

# Known issue
1. you need to insmod waterforce.ko every time you reboot

# WARNING: use at your own risk
