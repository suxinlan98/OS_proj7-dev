# Commands sent to the xv6 shell for the SMP concurrency stress test.
# Runs with multiple guest CPUs (--cpus 3): two background bigfile
# writers run concurrently with a foreground one on each mounted
# backend, then every file is verified, then stressfs runs inside
# each mount point.

mkdir /mnt
mkdir /fat
mount ext2 /mnt
mount fat32 /fat
bigfile /mnt/solo parallel /mnt/a /mnt/b
bigfile /mnt/a verify
bigfile /mnt/b verify
bigfile /fat/solo parallel /fat/a /fat/b
bigfile /fat/a verify
bigfile /fat/b verify
cd /mnt
stressfs
cd /
cd /fat
stressfs
cd /
umountstress
echo SMP_DONE
