# Waterforce X 240, 280, 360 Kernel module (POC)
1. Waterforce X 240 kernel Module proof of concept code
2. Modified code from https://github.com/aleksamagicka/waterforce-hwmon to add active CPU temp. feeding to the waterforce
3. RGB color based on the fan speed pump speed and cpu temperature

# dependency
apt-get install lm-sensors

# to build
make

# to test
sudo make dev

# to persistent install
sudo make modules_install

sudo depmod

# checking if the module is running
tail -f /var/log/syslog
it should show the CPU temperature every 2 secs


