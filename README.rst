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


Installing the driver
----------------------

Native Build and Installation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The driver can be built and installed natively using the following command:

.. code-block:: shell

   meson build
   ninja -C build install

Above command will install the driver in `/usr/local/bin/` directory and the
service file in `/etc/systemd/system/` directory.

Cross Build and Installation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The driver can be built for aarch64 using the following command:

.. code-block:: shell

   meson build --cross-file config/arm64_odyssey_linux_gcc
   ninja -C build

Above command will build the driver for aarch64. The binary can be found in
`build` directory. Copy the binary to the target board and install it in
`/usr/local/bin/` directory. The service file can be copied to
`/etc/systemd/system/` directory.


Driver application arguments
----------------------------

The driver takes the following arguments:

.. code-block:: shell

        odm_pf_driver [-c] [-l log_level] [-s]
        -c           : Enable console logging. Default is disabled.
        -l log_level : Set the log level. The default log level is LOG_INFO.
        -s           : Run selftest. Default is disabled.

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


Running the driver as a systemd Service
----------------------------------------

Installing and starting the service
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The driver can be started as a systemd service using the
`odm_pf_driver.service` file. Follow these steps to start the service:

1. Make sure the `odm_pf_driver` binary is installed in the `/usr/local/bin/`.
2. Make sure the `odm_pf_driver.service` file is installed in the
   `/etc/systemd/system/` directory.
3. Run the following commands:

   .. code-block:: shell

      sudo systemctl daemon-reload
      sudo systemctl enable odm_pf_driver.service
      sudo systemctl start odm_pf_driver.service

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

Uninstalling the driver
-----------------------

To uninstall the driver, run the following command:

.. code-block:: shell

   ninja -C build uninstall

This command will remove the driver binary from the `/usr/local/bin/` directory
and the service file from the `/etc/systemd/system/` directory.
