# SPDX-License-Identifier: Marvell-MIT
# Copyright (c) 2024 Marvell.

odm_pf_driver README
====================

Overview
--------

The odm_pf_driver is a userspace application that provides ODM PF driver
functionality. It manages the ODM PF device by initializing and configuring it,
and offering services to the ODM VF driver. Additionally, the driver includes a
self-test feature to verify its functionality.

Logging is handled via the syslog facility, with configurable log levels to
adjust the verbosity of messages. Console logging is also available to display
log messages directly on the console.

The driver can be run as a systemd service, allowing it to operate in the
background and manage the ODM PF device. Using the systemctl command, the
service can be started, stopped, and monitored. The driver supports various
arguments to control its behavior.

The service generates a VFIO token or UUID. It updates the odm_pf_driver.cfg
file with the newly generated UUID. The UUID will be generated once after every
boot. Users can refer to the odm_pf_driver.cfg file to get the UUID, which needs
to be passed as a VFIO token while using VFs.

The service will also unbind the ODM PF device from the current driver and bind
it to the vfio-pci driver.

Installing the driver
----------------------

Dependency Package
~~~~~~~~~~~~~~~~~~
The userspace PF driver depends on the uuid package. It can be installed as
follows:

.. code-block:: shell

   apt-get install uuid-runtime

Enable SRIOV for VFIO PCI
~~~~~~~~~~~~~~~~~~~~~~~~~

Update the kernel boot arguments with option ``vfio-pci.enable_sriov=1``.
Alternatively, you can load the ``vfio-pci`` module with ``enable_sriov``
parameter set.

.. code-block:: shell

   sudo modprobe vfio-pci enable_sriov=1


Native Build and Installation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The driver can be built and installed natively using the following command:

.. code-block:: shell

   meson build
   ninja -C build install

Above command will install the driver in `/usr/local/bin/` directory, the
service file in `/etc/systemd/system/` directory and config file and
script in `/etc`.

Cross Build and Installation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The driver can be built for aarch64 using the following command:

.. code-block:: shell

   meson build --cross-file config/arm64_odyssey_linux_gcc
   ninja -C build

Above command will build the driver for aarch64. The binary can be found in
`build` directory. Copy the binary to the target board and install it in
`/usr/local/bin/` directory. The service file can be copied to
`/etc/systemd/system/` directory. The config file and script can be copied
to `/etc`.

Driver application arguments
----------------------------

The driver takes the following arguments:

.. code-block:: shell

        odm_pf_driver [-c] [-l log_level] [-s] [-e eng_sel] [--num_vfs n]
        --vfio-vf-token uuid
        -c           : Enable console logging. Default is disabled.
        -l log_level : Set the log level. The default log level is LOG_INFO.
        -s           : Run selftest. Default is disabled.
        -e eng_sel   : Set the internal DMA engine to queue mapping.
        --vfio-vf-token uuid : Randomly generated VF token to be used by both PF
                               and VF.
        --num_vfs n : Create n number of VFs. Valid values are: 0,2,4,8,16. The
                      default value is 8.

When the log level is LOG_INFO, only log messages up to the INFO level are
displayed. The log levels correspond to the syslog levels are as follows:

- 0 - LOG_EMERG
- 1 - LOG_ALERT
- 2 - LOG_CRIT
- 3 - LOG_ERR
- 4 - LOG_WARNING
- 5 - LOG_NOTICE
- 6 - LOG_INFO
- 7 - LOG_DEBUG

``eng_sel`` is used to map the DMA engines to VF queues. There are 32 VF queues.
Each bit in the value corresponds to a DMA VF queue. A value of 0 will map DMA
engine-0 to that queue and a value of 1 will map DMA engine-1 to that queue.

- 0xCCCCCCCC - It will map queues and engines like below:
               Engine-0 to queues 0,1,4,5,8,9,12,13,16,17,20,21,24,25,28,29
               Engine-1 to queues 2,3,6,7,10,11,14,15,18,19,22,23,26,27,30,31

``uuid`` is a value generated using the command 'uuidgen'. This value needs to
be passed to both PF and VF as VFIO token.

``n`` is the number of VFs to create. If no value is passed, the default is
8VFs. The valid numbers of VFs are: 2,4,8,16.

Running the driver as a systemd Service
----------------------------------------

Installing and starting the service
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The driver can be started as a systemd service using the
`odm_pf_driver.service` file. Follow these steps to start the service:

1. Make sure the `odm_pf_driver` binary is installed in the `/usr/local/bin/`.
2. Make sure the `odm_pf_driver.service` file is installed in the
   `/etc/systemd/system/` directory.
3. Make sure the `odm_pf_driver.cfg` file is installed in the
   `/etc/` directory.
4. Make sure the `odm_pf_driver_prestart.sh` file is installed in the
   `/etc/` directory.
5. Run the following commands:

   .. code-block:: shell

      sudo systemctl daemon-reload
      sudo systemctl enable odm_pf_driver.service
      sudo systemctl start odm_pf_driver.service

6. Once the above files are installed at respective location, the service will
load automatically on every reboot and the steps 1 to 5 are not required.

Monitoring the Service
~~~~~~~~~~~~~~~~~~~~~~~

The service can be monitored using the following command:

.. code-block:: shell

   sudo journalctl -u odm_pf_driver.service -f

Stopping the Service
~~~~~~~~~~~~~~~~~~~~

The service can be stopped using the following command:

.. code-block:: shell

   sudo systemctl stop odm_pf_driver.service

Using driver arguments in the service
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The `ExecStart` line in the `odm_pf_driver.service` file can be updated with
the driver arguments. For example, to set the log level to LOG_DEBUG, the
`ExecStart` line can be updated as follows:

.. code-block:: shell

   ExecStart=/usr/local/bin/odm_pf_driver -l 7

After updating the `odm_pf_driver.service` file, run the following commands:

.. code-block:: shell

   sudo systemctl daemon-reload
   sudo systemctl restart odm_pf_driver.service

Using config file to update the arguments in the service
--------------------------------------------------------

The `odm_pf_driver.cfg` file is used to pass some command line arguments to the
PF driver. After any change in the config file, to reflect the changes, the
daemone need to be reloaded.

The location of file will be: /etc/odm_pf_driver.cfg.

Run the following commands to reload the daemon:

.. code-block:: shell

   sudo systemctl daemon-reload
   sudo systemctl restart odm_pf_driver.service

Make sure that no VFs are being used, when daemon gets reloaded.

The config file is used to pass/tune the below arguments:

``NUM_VFS`` specifies the number of VFs to be created. The ODM DMA device can
support up to 16 VFs. The default value of this is 8. To create the required
number of VFs, this value can be updated. This value is passed to the PF driver
with the option: ``--num_vfs``.

``ENG_SEL`` specifies the internal engine to queue mapping. The ODM DMA device
has two internal engines, Each queue can be mapped to one engine. Engine to
queue mapping is decided by a hardware register. This mapping can be tuned to
achieve higher performance. The default value is: 0xCCCCCCCC. The value in the
config file can be changed to alter the mapping. This value is passed to PF
driver with the option: ``-e``.

``UUID`` specifies the UUID token generated using uuidgen. Any application that
needs to use the VF should use the same value as the VFIO token. This value is
passed to PF driver with the option: ``--vfio-token``.

Uninstalling the driver
-----------------------

To uninstall the driver, run the following command:

.. code-block:: shell

   ninja -C build uninstall

This command will remove the driver binary from the `/usr/local/bin/` directory
and the service file from the `/etc/systemd/system/` directory.

Running the DPDK DMA autotest app
----------------------------------
Make sure the daemon is started and the PF userspace driver is loaded. Ensure
that VFs for the device are created. The number of VFs should be non-zero. This
can be verified as follows:

.. code-block:: shell

   cat /sys/bus/pci/devices/0000\:08\:00.0/sriov_numvfs

Generated VFIO token will be in the config file. It can be read as follows:

.. code-block:: shell

   cat /etc/odm_pf_driver.cfg

Run the DPDK application as follows:

.. code-block:: shell
        DPDK_TEST=dmadev_autotest ./dpdk-test
        --vfio-vf-token=<UUID value from config file> -a 0000:08:00.1
