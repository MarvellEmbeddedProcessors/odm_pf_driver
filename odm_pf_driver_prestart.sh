#!/bin/bash
# SPDX-License-Identifier: Marvell-MIT
# Copyright (c) 2024 Marvell.

FLAG_FILE="/var/run/uuid_generated"
CFG_FILE="/etc/odm_pf_driver.cfg"
PCI_DEVICE="0000:08:00.0"
DRIVER="vfio-pci"
UUID_LINE="UUID="

# Check if uuidgen is available
command -v uuidgen >/dev/null 2>&1 || { echo "uuid isn't installed"; exit 127; }

# Check if the UUID has already been generated
if [ ! -f "$FLAG_FILE" ]; then
    # Generate a new UUID
    NEW_UUID=$(uuidgen)
    echo "Generated UUID: $NEW_UUID"

    # Update the target file with the new UUID
    if grep -q "^$UUID_LINE" "$CFG_FILE"; then
        # Replace the existing UUID line
        sed -i "s/^$UUID_LINE.*/$UUID_LINE$NEW_UUID/" "$CFG_FILE"
    else
        # Append the UUID line if it doesn't exist
        echo "$UUID_LINE$NEW_UUID" >> "$CFG_FILE"
    fi

    # Create the flag file to indicate the UUID has been generated
    touch "$FLAG_FILE"
fi

echo "Unbinding PCI device $PCI_DEVICE"
echo $PCI_DEVICE > /sys/bus/pci/devices/$PCI_DEVICE/driver/unbind
echo "Binding PCI device $PCI_DEVICE to driver $DRIVER"
echo "177d a08b" > /sys/bus/pci/drivers/vfio-pci/new_id
exit 0
