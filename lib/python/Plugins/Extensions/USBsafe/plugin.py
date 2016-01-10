#!/usr/bin/env python
# -*- coding: UTF-8 -*-
##
## USBsafe for WetekPlay
##
## Copyright (c) 2012-2015 OpenLD
##          Javier Sayago <admin@lonasdigital.com>
## Contact: javilonas@esp-desarrolladores.com
##
## Licensed under the Apache License, Version 2.0 (the "License");
## you may not use this file except in compliance with the License.
## You may obtain a copy of the License at
##
##    http://www.apache.org/licenses/LICENSE-2.0
##
## Unless required by applicable law or agreed to in writing, software
## distributed under the License is distributed on an "AS IS" BASIS,
## WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
## See the License for the specific language governing permissions and
## limitations under the License.
##
##########################################################################
from Screens.Screen import Screen
from Components.Console import Console
from Screens.MessageBox import MessageBox
from Plugins.Plugin import PluginDescriptor
from Components.ActionMap import ActionMap
from Components.Sources.List import List
from Components.Label import Label
from Components.MenuList import MenuList
import os

class USBsafe(Screen):
	skin = '\n<screen position="center,center" size="750,400" title="Safely Remove USB Devices" >\n\t<ePixmap position="0,0" zPosition="-10" size="750,400" />\n        <widget name="lab_heading" position="30,40" size="700,30" font="Regular;25"    backgroundColor="black" foregroundColor="white" shadowOffset="-2,-2" shadowColor="black" transparent="1"/>\n\t<widget name="lab_usage" position="30,340" size="660,30" font="Regular;25"    backgroundColor="black" foregroundColor="grey" shadowOffset="-2,-2" shadowColor="black" transparent="1"/>\n\t<widget name="menulist_device" position="30,90" size="700,300" font="Fixed;22"  backgroundColor="background" foregroundColor="grey" shadowOffset="-2,-2" shadowColor="black" transparent="1"/>\n</screen>\n\t\t'

	def __init__(self, session):
		print '[USBsafe] started... '
		Screen.__init__(self, session)
		self.session = session
		self['actions'] = ActionMap(['OkCancelActions', 'ColorActions', 'DirectionActions'], {'cancel': self.exitPlugin,
		'ok': self.umountDevice,
		'red': self.exitPlugin,
		'green': self.umountDevice}, -1)
		self['lab_usage'] = Label(_('Select device and press OK to unmount or EXIT to quit'))
		self['lab_heading'] = Label(_('%-20s %-20s %-22s %9s' % (' Device', 'Mounted as', 'Type', 'Size')))
		self.wdg_list_dev = []
		self.list_dev = []
		self.noDeviceError = True
		self['menulist_device'] = MenuList(self.wdg_list_dev)
		self.getDevicesList()

	def exitPlugin(self):
		self.close()

	def umountDeviceConfirm(self, result):
		if result == True:
			Console().ePopen('umount -f %s 2>&1' % self.list_dev[self.selectedDevice], self.umountDeviceDone)

	def umountDeviceDone(self, result, retval, extra_args):
		if retval != 0:
			errmsg = '\n\n' + _('umount return code') + ': %s\n%s' % (retval, result)
			self.session.open(MessageBox, text=_('Cannot unmonut device. Please check whether recording is in progress, timeshit or some external tools (like samba, nfsd) that disable safe removal. Try to disable those actions/applications.') + ' ' + self.list_dev[self.selectedDevice] + errmsg, type=MessageBox.TYPE_ERROR, timeout=15)
		self.getDevicesList()

	def umountDevice(self):
		if self.noDeviceError == False:
			self.selectedDevice = self['menulist_device'].getSelectedIndex()
			self.session.openWithCallback(self.umountDeviceConfirm, MessageBox, text=_('Unmount and remove USB device at: ') + ' ' + self.list_dev[self.selectedDevice] + ' ?', type=MessageBox.TYPE_YESNO, timeout=10, default=False)

	def getDevicesList(self):
		self.wdg_list_dev = []
		self.list_dev = []
		self.noDeviceError = False
		file_mounts = '/proc/mounts'
		if os.path.exists(file_mounts):
			fd = open(file_mounts, 'r')
			lines_mount = fd.readlines()
			fd.close()
			for line in lines_mount:
				l = line.split(' ')
				if l[0][:7] == '/dev/sd':
					device = l[0][5:8]
					partition = l[0][5:9]
					size = '----'
					file_size = '/sys/block/%s/%s/size' % (device, partition)
					if os.path.exists(file_size):
						fd = open(file_size, 'r')
						size = fd.read()
						fd.close()
						size = size.strip('\n\r\t ')
						size = int(size) / 2048
					removable = '0'
					file_removable = '/sys/block/%s/removable' % device
					if os.path.exists(file_removable):
						fd = open(file_removable, 'r')
						removable = fd.read()
						fd.close()
						removable = removable.strip('\n\r\t ')
					self.list_dev.append(l[0])
					self.wdg_list_dev.append('%-12s %-16s %-11s %9s MB' % (l[0],
					l[1],
					l[2] + ',' + l[3][:2],
					size))

		if len(self.list_dev) == 0:
			self.noDeviceError = True
		self['menulist_device'].setList(self.wdg_list_dev)


def main(session, **kwargs):
	session.open(USBsafe)


def Plugins(**kwargs):
	plug = list()
	plug.append(PluginDescriptor(name='USB safe', description=_('Safely remove USB storage'), where=PluginDescriptor.WHERE_PLUGINMENU, icon='plugin.png', fnc=main))
	return plug
