#!/usr/bin/env python
# -*- coding: UTF-8 -*-
##
## WetekAudio for WetekPlay
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
from Plugins.Plugin import PluginDescriptor
from Components.Console import Console
from Components.Button import Button
from Components.ActionMap import ActionMap
from Components.ConfigList import ConfigList
from Components.config import config, configfile, ConfigSubsection, getConfigListEntry, ConfigSelection, ConfigYesNo
from Components.ConfigList import ConfigListScreen
from boxbranding import getImageDistro
import Screens.Standby
config.plugins.wea = ConfigSubsection()
config.plugins.wea.dcodec = ConfigSelection(default='0', choices=[('0', _('2CH PCM (default)')),
 ('1', _('DTS raw')),
 ('2', _('Dolby Digital')),
 ('3', _('DTS')),
 ('4', _('DD+')),
 ('5', _('DTSHD')),
 ('6', _('8CH PCM')),
 ('7', _('TrueHD'))])
config.plugins.wea.draw = ConfigSelection(default='0', choices=[('0', _('PCM (default)')),
 ('1', _('Raw - normal')),
 ('2', _('Raw - overclocked'))])
config.plugins.wea.amask = ConfigSelection(default='s', choices=[('l', _('Left mono - PCM only ')),
 ('r', _('Right mono - PCM only')),
 ('s', _('Stereo-Multi (default)')),
 ('c', _('Swap Left and Right'))])

class WetekAudio(ConfigListScreen, Screen):

	def __init__(self, session, args = None):
		self.skin = '\n\t\t\t<screen position="150,150" size="500,310" title="Wetek Audio Setup" >\n\t\t\t\t<widget name="config" position="20,15" size="460,250" scrollbarMode="showOnDemand" />\n\t\t\t\t<ePixmap position="40,265" size="140,40" pixmap="skin_default/buttons/green.png" alphatest="on" />\n\t\t\t\t<ePixmap position="180,265" size="140,40" pixmap="skin_default/buttons/red.png" alphatest="on" />\n\t\t\t\t<ePixmap position="320,265" size="140,40" pixmap="skin_default/buttons/yellow.png" alphatest="on" />\n\t\t\t\t<widget name="key_green" position="40,265" size="140,40" font="Regular;20" backgroundColor="#1f771f" zPosition="2" transparent="1" shadowColor="black" shadowOffset="-1,-1" />\n\t\t\t\t<widget name="key_red" position="180,265" size="140,40" font="Regular;20" backgroundColor="#9f1313" zPosition="2" transparent="1" shadowColor="black" shadowOffset="-1,-1" />\n\t\t\t\t<widget name="key_yellow" position="320,265" size="140,40" font="Regular;20" backgroundColor="#9f1313" zPosition="2" transparent="1" shadowColor="black" shadowOffset="-1,-1" />\n\t\t\t</screen>'
		Screen.__init__(self, session)
		self.onClose.append(self.abort)
		self.onChangedEntry = []
		self.list = []
		ConfigListScreen.__init__(self, self.list, session=self.session, on_change=self.changedEntry)
		self.createSetup()
		self.Console = Console()
		self['key_red'] = Button(_('Cancel'))
		self['key_green'] = Button(_('Save'))
		self['key_yellow'] = Button(_('Test'))
		self['setupActions'] = ActionMap(['SetupActions', 'ColorActions'], {'save': self.save,
		 'cancel': self.cancel,
		 'ok': self.save,
		 'yellow': self.Test}, -2)

	def createSetup(self):
		self.editListEntry = None
		self.list = []
		self.list.append(getConfigListEntry(_('PCM or Raw type :'), config.plugins.wea.draw))
		self.list.append(getConfigListEntry(_('Set Digital Codec :'), config.plugins.wea.dcodec))
		self.list.append(getConfigListEntry(_('Audio mask :'), config.plugins.wea.amask))
		self['config'].list = self.list
		self['config'].l.setList(self.list)
		return

	def changedEntry(self):
		for x in self.onChangedEntry:
			x()

		self.newConfig()

	def newConfig(self):
		print self['config'].getCurrent()[0]
		if self['config'].getCurrent()[0] == _('PCM or Raw type :'):
			self.createSetup()

	def abort(self):
		print 'aborting'

	def save(self):
		for x in self['config'].list:
			x[1].save()

		configfile.save()
		initWetA(self)
		self.close()

	def cancel(self):
		initWetA(self)
		for x in self['config'].list:
			x[1].cancel()

		self.close()

	def Test(self):
		self.createSetup()
		initWetA(self)


def initWetA(self = None):
	print '[WetekAudio] try to apply settings'
	try:
		f = open('/sys/class/audiodsp/digital_codec', 'w')
		f.write(config.plugins.wea.dcodec.getValue())
		f.close()
		f = open('/sys/class/audiodsp/digital_raw', 'w')
		f.write(config.plugins.wea.draw.getValue())
		f.close()
		self.current_service = self.session.nav.getCurrentlyPlayingServiceReference()
		self.session.nav.stopService()
		self.session.nav.playService(self.current_service)
		f = open('/sys/class/amaudio/audio_channels_mask', 'w')
		f.write(config.plugins.wea.amask.getValue())
		f.close()
	except:
		print '[WetekAudio] wet not present'


def startWEA(session, **kwargs):
	session.open(WetekAudio)


def main(menuid):
	if menuid == 'audio_menu':
		return [(_('WeTek Audio Setup'),
		  startWEA,
		  'Wetek Audio Setup',
		  None)]
	else:
		return []


WEASet = None

def autostartWEA(session, **kwargs):
	global WEASet
	WEASet = session.instantiateDialog(initWetA)


def Plugins(**kwargs):
	return [PluginDescriptor(where=PluginDescriptor.WHERE_SESSIONSTART, fnc=autostartWEA), PluginDescriptor(where=PluginDescriptor.WHERE_MENU, fnc=main)]
