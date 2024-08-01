# SPDX-License-Identifier: Marvell-MIT
# Copyright(C) 2024 Marvell.

project(
	'ODM userspace PF driver',
	'C',
	version: '24.06.0',
	license: 'Marvell-MIT',
	default_options: ['warning_level=2', 'werror=true']
)

cc = meson.get_compiler('c')
librt = cc.find_library('rt', required: true)
libpthread = cc.find_library('pthread', required: true)

subdir('src')

install_data('odm_pf_driver.service', install_dir: '/etc/systemd/system')
install_data('odm_pf_driver.cfg', install_dir: '/etc/')
install_data('odm_pf_driver_prestart.sh', install_dir: '/etc/')
