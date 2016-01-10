#!/usr/bin/env python
# -*- coding: UTF-8 -*-
##
## WetekSync for WetekPlay
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
from Plugins.Plugin import PluginDescriptor
from Screens.Screen import Screen
import os
from enigma import eTimer
from time import sleep

class LoopSyncMain(Screen):

	def __init__(self, session, args = None):
		Screen.__init__(self, session)
		self.session = session
		self.gotSession()

	def gotSession(self):
		self.debug = 0
		self.lstate = 0
		self.count1 = 0
		self.count2 = 0
		self.pts_diff_c = 0
		self.pts_diff_l = 0
		self.AVSyncTimer = eTimer()
		self.AVSyncTimer.callback.append(self.updateAVSync)
		self.AVSyncTimer.start(10000, True)

	def updateAVSync(self):
		self.current_service = self.session.nav.getCurrentlyPlayingServiceReference()
		pts_diff = 0
		max_diff = 21000
		try:
			f = open('/sys/class/tsync/pts_audio', 'r')
			pts_audio = int(f.read(), 16)
			f.close()
			f = open('/sys/class/tsync/pts_video', 'r')
			pts_video = int(f.read(), 16)
			f.close()
			pts_diff = abs(pts_audio - pts_video)
			f = open('/sys/class/amstream/bufs', 'r')
			line1 = f.read().replace('\n', '')
			f.close()
			strf1 = line1.find('Audio buffer:')
			strf2 = line1.find('buf bitrate latest:', strf1 + 1)
			strf3 = line1.find(',avg:', strf2 + 1)
			strf4 = line1.find('buf time after last pts:', strf3 + 1)
			bitrate = int(line1[strf3 + 5:strf4 - 4])
			if bitrate < 110000:
				max_diff = 40000
			if self.debug == 1:
				print '[WetekSync] *************************************'
				print '[WetekSync] LastState = ', self.lstate
				print '[WetekSync] pts_diff  = ', pts_diff
				print '[WetekSync] max_diff  = ', max_diff
				print '[WetekSync] a_bitrate = ', bitrate
				print '[WetekSync] Reset(s)  = ', self.count1
				print '[WetekSync] StopStart = ', self.count2
				print '[WetekSync] ptsdiff_c = ', self.pts_diff_c
				print '[WetekSync] *************************************'
		except Exception as e:
			print "[WetekSync] Can't read class"

		if self.lstate == 1:
			if self.debug == 1:
				print '[WetekSync] change_mode (2) !!!'
			try:
				f_tmp = open('/sys/class/tsync/mode', 'w')
				f_tmp.write('2')
				f_tmp.close()
			except Exception as e:
				print "[WetekSync] Can't change mode"

			self.lstate = 0
			pts_video = 0
		if self.pts_diff_l > max_diff and pts_diff > max_diff and pts_video != 0 and self.lstate == 0:
			if self.debug == 1:
				print '[WetekSync] change_mode (1) !!!'
			try:
				f_tmp = open('/sys/class/tsync/mode', 'w')
				f_tmp.write('1')
				f_tmp.close()
			except Exception as e:
				print "[WetekSync] Can't change mode"

			self.lstate = 1
			self.count1 += 1
		if pts_diff > 50000 and pts_video != 0:
			self.pts_diff_c += 1
		if pts_diff <= 50000 and pts_video != 0:
			self.pts_diff_c = 0
		if self.pts_diff_c > 2:
			if self.debug == 1:
				print '[WetekSync] DoAVSync !!!'
			self.session.open(DoAVSync)
			self.count2 += 1
			self.pts_diff_c = 0
		self.pts_diff_l = pts_diff
		self.AVSyncTimer.start(700, True)


class DoAVSync(Screen):
	skin = '\n\t\t<screen position="center,center" size="1920,1080" title="WetekSync" >\n\t\t</screen>'

	def __init__(self, session):
		Screen.__init__(self, session)
		try:
			f_tmp = open('/sys/class/video/blackout_policy', 'w')
			f_tmp.write('0')
			f_tmp.close()
		except Exception as e:
			print "[WetekSync] Can't change policy"

		self.current_service = self.session.nav.getCurrentlyPlayingServiceReference()
		self.session.nav.stopService()
		self.session.nav.playService(self.current_service)
		try:
			f_tmp = open('/sys/class/video/blackout_policy', 'w')
			f_tmp.write('1')
			f_tmp.close()
		except Exception as e:
			print "[WetekSync] Can't change policy"

		self.close()


def sessionstart(session, **kwargs):
	session.open(LoopSyncMain)


def Plugins(**kwargs):
	return [PluginDescriptor(where=[PluginDescriptor.WHERE_SESSIONSTART], fnc=sessionstart)]
