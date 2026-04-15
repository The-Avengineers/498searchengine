DATA_DEV=$1
MOUNT_DIR=/mnt/data

if [[ ! -b "$DATA_DEV" ]]; then
  echo "missing attached data disk at $DATA_DEV" >&2
  exit 1
fi

if ! blkid "$DATA_DEV" >/dev/null 2>&1; then
  mkfs.ext4 -F "$DATA_DEV"
fi

mkdir -p "$MOUNT_DIR"
UUID=$(blkid -s UUID -o value "$DATA_DEV")
if ! grep -q "UUID=${UUID}" /etc/fstab; then
  echo "UUID=${UUID} ${MOUNT_DIR} ext4 defaults,nofail 0 2" >>/etc/fstab
fi

if ! mountpoint -q "$MOUNT_DIR"; then
  mount "$MOUNT_DIR"
fi

systemctl daemon-reload
