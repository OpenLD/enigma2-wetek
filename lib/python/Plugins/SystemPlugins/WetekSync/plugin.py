#!/usr/bin/env python
# -*- coding: UTF-8 -*-
##
## WetekSync for WetekPlay
##
## Copyright (c) 2012-2016 OpenLD
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
from Plugins.Plugin import PluginDescriptor
from Screens.Screen import Screen
import os
from enigma import eTimer

class LoopSyncMain(Screen):

	def __init__(self, session, args = None):
		Screen.__init__(self, session)
		self.session = session
		self.gotSession()

	def gotSession(self):
		self.ResetFlag()
		self.AVSyncTimer = eTimer()
		self.AVSyncTimer.callback.append(self.UpdateStatus)
		self.AVSyncTimer.start(10000, True)

	def UpdateStatus(self):
		frontendDataOrg = ""
		service1 = self.session.nav.getCurrentlyPlayingServiceOrGroup()
		if service1:
			service = self.session.nav.getCurrentService()
			if service:
				feinfo = service.frontendInfo()
				frontendDataOrg = feinfo and feinfo.getAll(True)
				if frontendDataOrg:		### DVB-S/C/T ###
					if self.CheckFlag():
						print "[WetekSync] DoWetekSync !!!"
						self.AVSyncTimer.start(500, True)
						self.ResetFlag()
						try:
							self.session.open(DoWetekSync,service1)
						except Exception, e:
							print "[WetekSync] Can't WetekSync"
					self.AVSyncTimer.start(100, True)
					return
				else:		### IPTV or VOD ###
					self.ResetFlag()
					self.AVSyncTimer.start(500, True)
					return
			else:
				self.AVSyncTimer.start(500, True)
				return

	def CheckFlag(self):
		try:
			if int(open("/sys/class/tsync/reset_flag", "r").read(),16) == 1: return True;
		except Exception as e:
			print "[WetekSync] Can't read class"
			self.AVSyncTimer.start(500, True)
		return False;

	def ResetFlag(self):
		try:
			open("/sys/class/tsync/reset_flag", "w").write("0")
		except Exception, e:
			print "[WetekSync] Can't ResetFlag"

class DoWetekSync(Screen):
	skin = '\n\t\t<screen position="center,center" size="1920,1080" title="" >\n\t\t</screen>'

	def __init__(self, session, xxx):
		Screen.__init__(self, session)
		try:
			open("/sys/class/video/blackout_policy", "w").write("0")
		except Exception as e:
			print "[WetekSync] Can't change policy(0)"
		self.session.nav.stopService()
		self.session.nav.playService(xxx)
		try:
			open("/sys/class/video/blackout_policy", "w").write("1")
		except Exception as e:
			print "[WetekSync] Can't change policy(1)"
		self.close()


def sessionstart(session, **kwargs):
	session.open(LoopSyncMain)

def Plugins(**kwargs):
	return [PluginDescriptor(where=[PluginDescriptor.WHERE_SESSIONSTART], fnc=sessionstart)]
