sudo dnf install -y gcc make kernel-devel-$(uname -r)
make

sudo insmod cpuctl.ko
ls -l /dev/cpuctl
cat /dev/cpuctl  


# выключить CPU N
echo "N off" | sudo tee /dev/cpuctl

# включить CPU N
echo "N on"  | sudo tee /dev/cpuctl

cat /dev/cpuctl

htop

journalctl -k -n 100


