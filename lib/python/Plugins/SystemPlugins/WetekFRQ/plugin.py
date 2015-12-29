from Screens.Screen import Screen
from Plugins.Plugin import PluginDescriptor
from Components.Console import Console
from Components.Button import Button
from Components.ActionMap import ActionMap
from Components.ConfigList import ConfigList
from Components.config import config, configfile, ConfigSubsection, getConfigListEntry, ConfigSelection
from Components.ConfigList import ConfigListScreen
import Screens.Standby

config.plugins.wetek = ConfigSubsection()

config.plugins.wetek.governor = ConfigSelection(default='ondemand', choices=[('hotplug', _('hotplug')),
('interactive', _('interactive')),
('conservative', _('conservative')),
('ondemand', _('ondemand (default)')),
('performance', _('performance'))])
config.plugins.wetek.workfrq = ConfigSelection(default='1200000', choices=[('200000', _('200MHz')),
('600000', _('600MHz')),
('792000', _('792MHz')),
('816000', _('816MHz')),
('840000', _('840MHz')),
('984000', _('984MHz')),
('1000000', _('1GHz')),
('1080000', _('1.08GHz')),
('1200000', _('1.2GHz (default)')),
('1320000', _('1.32GHz')),
('1512000', _('1.512GHz'))])
config.plugins.wetek.stdbyfrq = ConfigSelection(default='792000', choices=[('96000', _('96MHz')),
('200000', _('200MHz')),
('600000', _('600MHz')),
('792000', _('792MHz (default)')),
('816000', _('816MHz')),
('840000', _('840MHz')),
('984000', _('984MHz')),
('1000000', _('1GHz')),
('1080000', _('1.08GHz')),
('1200000', _('1.2GHz'))])

def leaveStandby():
    print '[WetekFRQ] Leave Standby'
    initBooster()


def standbyCounterChanged(configElement):
    print '[WetekFRQ] In Standby'
    initStandbyBooster()
    from Screens.Standby import inStandby
    inStandby.onClose.append(leaveStandby)


def initBooster():
    print '[WetekFRQ] initBooster'
    try:
        f = open('/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq', 'w')
        f.write(config.plugins.wetek.workfrq.getValue())
        f.close()
        f = open('/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor', 'w')
        f.write(config.plugins.wetek.governor.getValue())
        f.close()
    except:
        pass


def initStandbyBooster():
    print '[WetekFRQ] initStandbyBooster'
    try:
        f = open('/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq', 'w')
        f.write(config.plugins.wetek.stdbyfrq.getValue())
        f.close()
    except:
        pass


class WetekFRQ(ConfigListScreen, Screen):

    def __init__(self, session, args = None):
        self.skin = '\n\t\t\t<screen position="150,150" size="500,210" title="Wetek CPU Frequency Setup" >\n\t\t\t\t<widget name="config" position="20,15" size="460,150" scrollbarMode="showOnDemand" />\n\t\t\t\t<ePixmap position="40,165" size="140,40" pixmap="skin_default/buttons/green.png" alphatest="on" />\n\t\t\t\t<ePixmap position="180,165" size="140,40" pixmap="skin_default/buttons/red.png" alphatest="on" />\n\t\t\t\t<widget name="key_green" position="40,165" size="140,40" font="Regular;20" backgroundColor="#1f771f" zPosition="2" transparent="1" shadowColor="black" shadowOffset="-1,-1" />\n\t\t\t\t<widget name="key_red" position="180,165" size="140,40" font="Regular;20" backgroundColor="#9f1313" zPosition="2" transparent="1" shadowColor="black" shadowOffset="-1,-1" />\n\t\t\t</screen>'
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
        self.list.append(getConfigListEntry(_('OpenLD max CPU frequency'), config.plugins.wetek.workfrq))
        self.list.append(getConfigListEntry(_('Standby max CPU frequency'), config.plugins.wetek.stdbyfrq))
        self.list.append(getConfigListEntry(_('Scaling governor'), config.plugins.wetek.governor))
        self['config'].list = self.list
        self['config'].l.setList(self.list)

    def changedEntry(self):
        for x in self.onChangedEntry:
            x()

        self.newConfig()

    def newConfig(self):
        print self['config'].getCurrent()[0]
        if self['config'].getCurrent()[0] == _('Start Boot Frequency'):
            self.createSetup()

    def abort(self):
        print 'aborting'

    def save(self):
        for x in self['config'].list:
            x[1].save()

        configfile.save()
        initBooster()
        self.close()

    def cancel(self):
        initBooster()
        for x in self['config'].list:
            x[1].cancel()

        self.close()

    def Test(self):
        self.createSetup()
        initBooster()


class WETEK_Booster:

    def __init__(self, session):
        print '[WetekFRQ] initializing'
        self.session = session
        self.service = None
        self.onClose = []
        self.Console = Console()
        initBooster()

    def shutdown(self):
        self.abort()

    def abort(self):
        print '[WetekFRQ] aborting'

    config.misc.standbyCounter.addNotifier(standbyCounterChanged, initial_call=False)


def main(menuid):
    if menuid != 'system':
        return []
    return [(_('Wetek CPU Control'),
      startBooster,
      'Wetek CPU Control',
      None)]


def startBooster(session, **kwargs):
    session.open(WetekFRQ)


wbooster = None
gReason = -1
mySession = None

def wetekbooster():
    global wbooster
    global mySession
    global gReason
    if gReason == 0 and mySession != None and wbooster == None:
        print '[WetekFRQ] Starting !!'
        wbooster = WETEK_Booster(mySession)
    elif gReason == 1 and wbooster != None:
        print '[WetekFRQ] Stopping !!'
        wbooster = None


def sessionstart(reason, **kwargs):
    global mySession
    global gReason
    print '[WetekFRQ] sessionstart'
    if kwargs.has_key('session'):
        mySession = kwargs['session']
    else:
        gReason = reason
    wetekbooster()


def Plugins(**kwargs):
    return [PluginDescriptor(where=[PluginDescriptor.WHERE_AUTOSTART, PluginDescriptor.WHERE_SESSIONSTART], fnc=sessionstart), PluginDescriptor(name='Wetek FRQ Setup', description='Set CPU speed settings', where=PluginDescriptor.WHERE_MENU, fnc=main)]
