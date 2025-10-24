sudo dnf install -y gcc make kernel-devel-$(uname -r)
make

sudo insmod hwpoison_ctl.ko
ls -l /dev/hwpoison
cat /dev/hwpoison


echo "soft 256"   | sudo tee /dev/hwpoison

echo "softpfn 123456" | sudo tee /dev/hwpoison
echo "hard 123456"    | sudo tee /dev/hwpoison

echo "block off N" | sudo tee /dev/hwpoison
echo "block on N"  | sudo tee /dev/hwpoison


journalctl -k -n 200 | grep -i hwpoison