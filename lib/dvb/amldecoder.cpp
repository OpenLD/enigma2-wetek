/** @file
 * This file add support for Amlogic video decoding to enigma2
 * Copyright (C) 2015  Christian Ege <k4230r6@gmail.com>
 *
 * This file is part of Enigma2
 *
 * AMLDecocder is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * AMLDecocder is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with AMLDecocder.  If not, see <http://www.gnu.org/licenses/>.
 */

// Modul includes
#include <lib/dvb/amldecoder.h>
// Project includes

#include <lib/base/cfile.h>
#include <lib/base/ebase.h>
#include <lib/base/eerror.h>
#include <lib/base/wrappers.h>
#include <lib/components/tuxtxtapp.h>

// Kernel includes
#include <linux/dvb/audio.h>
#include <linux/dvb/video.h>
#include <linux/dvb/dmx.h>

// System includes
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include <pthread.h>


extern "C" {
#include <dvbcsa/dvbcsa.h>
}

#define TRACE__ eDebug("%s(%d): ",__PRETTY_FUNCTION__,__LINE__);

//#define SHOW_WRITE_TIME

static void signal_handler(int x)
{
	TRACE__;
}

/*
 * This transfers CSA keys from kernel space
 */
struct aml_dsc {
	uint8_t	even[8];
	uint8_t	odd[8];
}__attribute__((packed));;

eFilePushThreadDecorder::eFilePushThreadDecorder(size_t buffersize):
	m_fd_source(-1),
	m_ca_fd(-1),
	m_buffersize(buffersize),
	m_buffer((unsigned char*)malloc(buffersize)),
	m_pesbuffer((unsigned char*)malloc(buffersize)),
	m_overflow_count(0),
	m_stop(1),
	m_messagepump(eApp, 0),
	mp_codec(0)
{
	TRACE__
	char filename[32];
	snprintf(filename, sizeof(filename), "/dev/ca%dloop",0);
	m_ca_fd =  ::open(filename, (O_RDONLY | O_NONBLOCK));
	if(m_ca_fd < 0)
	{
		perror("CA DEVICE");
		eDebug("%s() unable to open ca device",__PRETTY_FUNCTION__);
	}
	else
	{
		eDebug("%s() SUCESSFULLY OPENED CA DEVICE!!!!!!!!!!!!!!!!",__PRETTY_FUNCTION__);
	}

	/* soft csa */
	cs = dvbcsa_bs_batch_size();
	eDebug("CSA batch_size=%d", cs);
	cs_tsbbatch_even = (dvbcsa_bs_batch_s *) malloc((cs + 1) * sizeof(struct dvbcsa_bs_batch_s));
	cs_tsbbatch_odd = (dvbcsa_bs_batch_s *) malloc((cs + 1) * sizeof(struct dvbcsa_bs_batch_s));
	cs_key_even = dvbcsa_bs_key_alloc();
	cs_key_odd = dvbcsa_bs_key_alloc();
	/* */


	CONNECT(m_messagepump.recv_msg, eFilePushThreadDecorder::recvEvent);
}

eFilePushThreadDecorder::~eFilePushThreadDecorder()
{
	stop(); /* eThread is borked, always call stop() from d'tor */
	free(m_buffer);
	free(m_pesbuffer);
	m_buffer = NULL;
	::close(m_ca_fd);

	/* soft csa */
	if (cs_key_even)
		dvbcsa_bs_key_free(cs_key_even);
	if (cs_key_odd)
		dvbcsa_bs_key_free(cs_key_odd);

	free(cs_tsbbatch_even);
	free(cs_tsbbatch_odd);
}

/* soft csa */
unsigned char ts_packet_get_payload_offset(unsigned char *ts_packet)
{
  if (ts_packet[0] != 0x47)
    return 0;

  unsigned char adapt_field   = (ts_packet[3] &~ 0xDF) >> 5; // 11x11111
  unsigned char payload_field = (ts_packet[3] &~ 0xEF) >> 4; // 111x1111

  if (!adapt_field && !payload_field)     // Not allowed
    return 0;

  if (adapt_field)
  {
    unsigned char adapt_len = ts_packet[4];
    if (payload_field && adapt_len > 182) // Validity checks
      return 0;
    if (!payload_field && adapt_len > 183)
      return 0;
    if (adapt_len + 4 > 188)  // adaptation field takes the whole packet
      return 0;
    return 4 + 1 + adapt_len;     // ts header + adapt_field_len_byte + adapt_field_len
  }
  else
  {
    return 4; // No adaptation, data starts directly after TS header
  }
}
/* */

void eFilePushThreadDecorder::thread()
{
	TRACE__

// ioPrio fixed, but disable to prevent EPG update freeze
//	setIoPrio(IOPRIO_CLASS_RT, 0);

//  Try with prio 50 (0..99) probably to high, and SCHED_RR as SCHED_FIFO may be too punishing
//  In the worst case scenario try cpu affinity and switch to other core
	sched_param sch;
	int policy; 
//    pthread_getschedparam(native_handle(), &policy, &sch);
	pthread_getschedparam(pthread_self(), &policy, &sch);
	sch.sched_priority = 50;
	if(pthread_setschedparam(pthread_self(), SCHED_OTHER, &sch)) {
		eDebug("-.-.-[eFilePushThreadDecorder] Failed to setschedparam ");
	}
	else {
		eDebug("-.-.-[eFilePushThreadDecorder] setschedparam changed! ");
	}

	eDebug("[eFilePushThreadDecorder] THREAD START");

	/* we set the signal to not restart syscalls, so we can detect our signal. */
	struct sigaction act;
	act.sa_handler = signal_handler; // no, SIG_IGN doesn't do it. we want to receive the -EINTR
	act.sa_flags = 0;
	sigaction(SIGUSR1, &act, 0);

	ssize_t len, len_ts = 0;
	struct pollfd pfd[2];
	ssize_t isize = 0;
	int ret;
	struct aml_dsc keys;

	/* soft csa */
	int ccs = 0;
	int payload_len, offset;
	int cs_fill_even = 0;
	int cs_fill_odd = 0;

	hasStarted();



	/* m_stop must be evaluated after each syscall. */
	while (!m_stop)
	{
		pfd[0].fd = m_fd_source;
		pfd[0].events = POLLIN;

		if(0 < m_ca_fd)
		{ /*maybe kernel does not provide ca device */
			pfd[1].fd = m_ca_fd;
			pfd[1].events = POLLIN;
		}

		//eDebug("[eFilePushThreadDecorder] polling");
		if (poll(pfd,2,3000))
		{
			if(pfd[1].revents & POLLIN)
			{
				len = read(m_ca_fd, &keys, sizeof(aml_dsc));
				if (len < 0)
				{
					perror("[eFilePushThreadDecorder] error during read of keys");
				}
				else
				{
					// Update Keys for next packages
					dvbcsa_bs_key_set(keys.even, cs_key_even);
					dvbcsa_bs_key_set(keys.odd, cs_key_odd);
				}
			}
			if (pfd[0].revents & POLLIN)
			{
				//eDebug("[eFilePushThreadDecorder] reading");
				len_ts += read(m_fd_source, m_buffer + len_ts, m_buffersize - len_ts - 188);
				if (len_ts < 0){
						perror("[eFilePushThreadDecorder] error in read: ");
				}
				else if(len_ts < (64 * 1024) )
					continue;
				else
				{
					ccs = 0;				
					for (isize = 0; isize < len_ts; isize += 188)
					{
						if (m_buffer[isize + 3] & 0xC0)
						{
							unsigned int ev_od = m_buffer[isize + 3]&0xC0;
							offset = ts_packet_get_payload_offset(m_buffer + isize);
							payload_len = 188 - offset;
							m_buffer[isize + 3] &= 0x3f;  // consider it decrypted now

							if (((ev_od & 0x40) >> 6) == 0)
							{
								cs_tsbbatch_even[cs_fill_even].data = &m_buffer[isize + offset];
								cs_tsbbatch_even[cs_fill_even].len = payload_len;
								cs_fill_even++;
							}
							else
							{
								cs_tsbbatch_odd[cs_fill_odd].data = &m_buffer[isize + offset];
								cs_tsbbatch_odd[cs_fill_odd].len = payload_len;
								cs_fill_odd++;
							}
							// Check if current batch is complete for decryption
							if (++ccs >= cs)
							{
								// start a new batch in next cycle
								ccs = 0;

								// decrypt even packages 
								if (cs_fill_even)
								{
									cs_tsbbatch_even[cs_fill_even].data = NULL;
									dvbcsa_bs_decrypt(cs_key_even, cs_tsbbatch_even, 184);
									cs_fill_even = 0;
								}

								// decrypt odd packages
								if (cs_fill_odd)
								{
									cs_tsbbatch_odd[cs_fill_odd].data = NULL;
									dvbcsa_bs_decrypt(cs_key_odd, cs_tsbbatch_odd, 184);
									cs_fill_odd = 0;
								}
							}
						}

					}

					// check if there is residue TS packets left for decoding (css < cs)
					if (ccs)
					{
						// decrypt even packages
						if (cs_fill_even)
						{
							cs_tsbbatch_even[cs_fill_even].data = NULL;
							dvbcsa_bs_decrypt(cs_key_even, cs_tsbbatch_even, 184);
							cs_fill_even = 0;
						}
						
						// decrypt odd packages
						if (cs_fill_odd)
						{
							cs_tsbbatch_odd[cs_fill_odd].data = NULL;
							dvbcsa_bs_decrypt(cs_key_odd, cs_tsbbatch_odd, 184);
							cs_fill_odd = 0;
						}

					}


					isize = 0;
					do{
						ret = codec_write(mp_codec, m_buffer + isize, len_ts - isize);
						if (ret < 0) {
							eDebug("[eFilePushThreadDecorder] codec write data failed, errno %d", errno);							
						}
						else {						
							isize += ret;
						}
					}while(isize < len_ts);				
					len_ts = 0;
				}
			}
		}
	}
	flush();
	sendEvent(evtStopped);
	eDebug("[eFilePushThreadDecorder] THREAD STOP");
}

void eFilePushThreadDecorder::start(int fd,codec_para_t* codec)
{
	TRACE__

	m_fd_source = fd;
	m_stop = 0;
	mp_codec = codec;

	if(mp_codec)
	{
		run();
	}
}

void eFilePushThreadDecorder::stop()
{
	TRACE__

	/* if we aren't running, don't bother stopping. */
	if (m_stop == 1)
		return;
	m_stop = 1;
	eDebug("[eFilePushThreadDecorder] stopping thread."); /* just do it ONCE. it won't help to do this more than once. */
	sendSignal(SIGUSR1);
	kill();
}

void eFilePushThreadDecorder::sendEvent(int evt)
{
	TRACE__
	m_messagepump.send(evt);
}

void eFilePushThreadDecorder::recvEvent(const int &evt)
{
	TRACE__
	m_event(evt);
}

void eFilePushThreadDecorder::flush()
{
	TRACE__
}

DEFINE_REF(eAMLTSMPEGDecoder);


eAMLTSMPEGDecoder::eAMLTSMPEGDecoder(eDVBDemux *demux, int decoder)
	: m_demux(demux),
		m_vpid(-1), m_vtype(-1), m_apid(-1), m_atype(-1), m_pcrpid(-1), m_textpid(-1),
		m_changed(0), m_decoder(decoder), m_video_clip_fd(-1), m_showSinglePicTimer(eTimer::create(eApp))
{
	TRACE__
	if (m_demux)
	{
		m_demux->connectEvent(slot(*this, &eAMLTSMPEGDecoder::demux_event), m_demux_event_conn);
	}
	memset(&m_codec, 0, sizeof(codec_para_t ));
	CONNECT(m_showSinglePicTimer->timeout, eAMLTSMPEGDecoder::finishShowSinglePic);
	m_state = stateStop;
}

eAMLTSMPEGDecoder::~eAMLTSMPEGDecoder()
{
	TRACE__
	finishShowSinglePic();
	/* Stop playback thread */
	m_threadDecoder.stop();
	m_vpid = m_apid = m_pcrpid = m_textpid = pidNone;
	m_changed = -1;
	setState();

	if(0 < m_video_dmx_fd)
	{
		::close(m_video_dmx_fd);
		m_video_dmx_fd = -1;
	}

	if(0 < m_audio_dmx_fd)
	{
		::close(m_audio_dmx_fd);
		m_audio_dmx_fd = -1;
	}
	if(0 < m_pvr_fd)
	{
		eDebug("%s() Closing DVR0 device ",__PRETTY_FUNCTION__);
		int ret = ::close(m_pvr_fd);
		eDebug("%s() result %d ",__PRETTY_FUNCTION__,ret);
		m_pvr_fd = -1;
	}

	struct buf_status vbuf;
	do {
		int ret = codec_get_vbuf_state(&m_codec, &vbuf);
		if (ret != 0) {
			eDebug("codec_get_vbuf_state error: %x", -ret);
			break;
		}
	} while (vbuf.data_len > 0x100);

	codec_close(&m_codec);

}


int eAMLTSMPEGDecoder::setState()
{
	TRACE__
	int res = 0;
	eDebug("%s() vpid=%d, apid=%d",__PRETTY_FUNCTION__, m_vpid, m_apid);
	return res;
}

int eAMLTSMPEGDecoder::m_pcm_delay=-1,
	eAMLTSMPEGDecoder::m_ac3_delay=-1;

RESULT eAMLTSMPEGDecoder::setHwPCMDelay(int delay)
{
	TRACE__
	return 0;
}

RESULT eAMLTSMPEGDecoder::setHwAC3Delay(int delay)
{
	TRACE__
	return 0;
}


RESULT eAMLTSMPEGDecoder::setPCMDelay(int delay)
{
	TRACE__
	return m_decoder == 0 ? setHwPCMDelay(delay) : -1;
}

RESULT eAMLTSMPEGDecoder::setAC3Delay(int delay)
{
	TRACE__
	return m_decoder == 0 ? setHwAC3Delay(delay) : -1;
}


RESULT eAMLTSMPEGDecoder::setVideoPID(int vpid, int type)
{
	TRACE__
	if ((m_vpid != vpid) || (m_vtype != type))
	{
		m_changed |= changeVideo;
		m_vpid = vpid;
		m_vtype = type;
		m_codec.video_type = VFORMAT_MPEG12;
		switch (type)
		{
			default:
			case MPEG2:
			case MPEG1:
			eDebug("%s() video type: MPEG1/2",__PRETTY_FUNCTION__);
			break;
			case MPEG4_H264:
			m_codec.video_type = VFORMAT_H264;
			eDebug("%s() video type: MPEG4 H264",__PRETTY_FUNCTION__);
			break;
			case MPEG4_Part2:
			m_codec.video_type = VFORMAT_MPEG4; //maybe?
			eDebug("%s() video type: MPEG4 Part2",__PRETTY_FUNCTION__);
			break;
		}
		eDebug("%s() vpid=%d, type=%d",__PRETTY_FUNCTION__, vpid, type);
		char filename[32];
		snprintf(filename, sizeof(filename), "/dev/dvb/adapter%d/demux%d", 0, 0);
		m_video_dmx_fd =  ::open(filename, O_RDWR | O_CLOEXEC);
		if(m_video_dmx_fd < 0)
		{
			perror("DEMUX DEVICE");
			eDebug("%s() unable to open Demuxer device",__PRETTY_FUNCTION__);
		}
		else
		{
#if 0		
			struct dmx_pes_filter_params pesFilterParams;
			pesFilterParams.pid = vpid;
			pesFilterParams.input = DMX_IN_FRONTEND;
			pesFilterParams.output = DMX_OUT_TS_TAP;
			pesFilterParams.pes_type = DMX_PES_OTHER;
			pesFilterParams.flags = DMX_IMMEDIATE_START;
			if (ioctl(m_video_dmx_fd, DMX_SET_PES_FILTER, &pesFilterParams) < 0)
			{
				perror("DMX_SET_PES_FILTER");
				eDebug("%s() VPID unable to set DMX_SET_PES_FILTER",__PRETTY_FUNCTION__);
			}
#endif			
		}
	}
	return 0;
}

RESULT eAMLTSMPEGDecoder::setAudioPID(int apid, int type)
{
	TRACE__
	/* do not set an audio pid on decoders without audio support */
	//if (!m_has_audio) apid = -1;

	if ((m_apid != apid) || (m_atype != type))
	{
		m_changed |= changeAudio;
		m_atype = type;
		m_apid = apid;
		m_codec.audio_type = AFORMAT_MPEG;
		switch (type)
		{
			default:
			case aMPEG:
			eDebug("%s() audio type: MPEG",__PRETTY_FUNCTION__);
			break;
			case aAC3:
			m_codec.audio_type = AFORMAT_AC3;
			eDebug("%s() audio type: AC3",__PRETTY_FUNCTION__);
			break;
			case aAAC:
			m_codec.audio_type = AFORMAT_AAC;
			eDebug("%s() audio type: AAC",__PRETTY_FUNCTION__);
			break;
			case aDTS:
			m_codec.audio_type = AFORMAT_DTS;
			eDebug("%s() audio type: DTS",__PRETTY_FUNCTION__);
			break;
			case aAACHE:
			m_codec.audio_type = AFORMAT_AAC_LATM;
			eDebug("%s() audio type: AAC_LATM",__PRETTY_FUNCTION__);
			break;

		}
		eDebug("%s() apid=%d, type=%d",__PRETTY_FUNCTION__, apid, type);
		char filename[32];
		snprintf(filename, sizeof(filename), "/dev/dvb/adapter%d/demux%d", 0, 0);
		m_audio_dmx_fd =  ::open(filename, O_RDWR | O_CLOEXEC);
		if(m_audio_dmx_fd < 0)
		{
			perror("DEMUX DEVICE");
			eDebug("%s() unable to open Demuxer device",__PRETTY_FUNCTION__);
		}
		else
		{
#if 0		
			struct dmx_pes_filter_params pesFilterParams;
			pesFilterParams.pid = apid;
			pesFilterParams.input = DMX_IN_FRONTEND;
			pesFilterParams.output = DMX_OUT_TS_TAP;
			pesFilterParams.pes_type = DMX_PES_OTHER;
			pesFilterParams.flags = DMX_IMMEDIATE_START;
			if (ioctl(m_audio_dmx_fd, DMX_SET_PES_FILTER, &pesFilterParams) < 0)
			{
				perror("DMX_SET_PES_FILTER");
				eDebug("%s() unable to set DMX_SET_PES_FILTER",__PRETTY_FUNCTION__);
			}
#endif			
		}
	}
	return 0;
}

int eAMLTSMPEGDecoder::m_audio_channel = -1;

RESULT eAMLTSMPEGDecoder::setAudioChannel(int channel)
{
	TRACE__
	if (channel == -1)
		channel = ac_stereo;
	if (m_decoder == 0 && m_audio_channel != channel)
	{
/*
		if (m_audio)
		{
			m_audio->setChannel(channel);
			m_audio_channel=channel;
		}
		else
			eDebug("eAMLTSMPEGDecoder::setAudioChannel but no audio decoder exist");
*/
	}
	return 0;
}

int eAMLTSMPEGDecoder::getAudioChannel()
{
	TRACE__
	return m_audio_channel == -1 ? ac_stereo : m_audio_channel;
}

RESULT eAMLTSMPEGDecoder::setSyncPCR(int pcrpid)
{
	int fd;
	char *path = "/sys/class/tsdemux/pcr_pid";
	char  bcmd[16];
	
	TRACE__
	eDebug("eAMLTSMPEGDecoder::setSyncPCR %d",pcrpid);
	m_pcrpid = pcrpid;
	
	fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd >= 0) {
		sprintf(bcmd, "%d", pcrpid);
		write(fd, bcmd, strlen(bcmd));
		close(fd);
	}
	
	return 0;
}

RESULT eAMLTSMPEGDecoder::setTextPID(int textpid)
{
	TRACE__
	eDebug("%s() m_textpid=%d",__PRETTY_FUNCTION__, textpid);
	return 0;
}

RESULT eAMLTSMPEGDecoder::setSyncMaster(int who)
{
	TRACE__
	return 0;
}

RESULT eAMLTSMPEGDecoder::set()
{
	TRACE__
	return 0;
}

int eAMLTSMPEGDecoder::osdBlank(char *path,int cmd)
{
	int fd;
	char  bcmd[16];
	fd = open(path, O_CREAT|O_RDWR | O_TRUNC, 0644);

	if(fd>=0) {
		sprintf(bcmd,"%d",cmd);
		write(fd,bcmd,strlen(bcmd));
		close(fd);
		return 0;
	}

	return -1;
}

int eAMLTSMPEGDecoder::setAvsyncEnable(int enable)
{
	int fd;
	char *path = "/sys/class/tsync/enable";
	char  bcmd[16];
	fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd >= 0) {
		sprintf(bcmd, "%d", enable);
		write(fd, bcmd, strlen(bcmd));
		close(fd);
		return 0;
	}

	return -1;
}

int eAMLTSMPEGDecoder::setDisplayAxis(int recovery)
{
	int fd;
	char *path = "/sys/class/display/axis";
	char str[128];
	fd = open(path, O_CREAT|O_RDWR | O_TRUNC, 0644);
	if (fd >= 0) {
		if (!recovery) {
			read(fd, str, 128);
			printf("read axis %s, length %d\n", str, strlen(str));
		}
		if (recovery) {
			sprintf(str, "%d %d %d %d %d %d %d %d",
				m_axis[0],m_axis[1], m_axis[2], m_axis[3], m_axis[4], m_axis[5], m_axis[6], m_axis[7]);
		} else {
			sprintf(str, "2048 %d %d %d %d %d %d %d",
				m_axis[1], m_axis[2], m_axis[3], m_axis[4], m_axis[5], m_axis[6], m_axis[7]);
		}
		write(fd, str, strlen(str));
		close(fd);
		return 0;
	}
	return -1;
}

int eAMLTSMPEGDecoder::setStbSourceHiu()
{
	int fd;
	char *path = "/sys/class/stb/source";
	char  bcmd[16];
	fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd >= 0) {
		sprintf(bcmd, "%s", "hiu");
		write(fd, bcmd, strlen(bcmd));
		close(fd);
		printf("set stb source to hiu!\n");
		return 0;
	}
	return -1;
}

int eAMLTSMPEGDecoder::setStbDemuxSourceHiu()
{
	int fd;
	char *path = "/sys/class/stb/demux1_source"; // use demux0 for record, and demux1 for playback
	char  bcmd[16];
	fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd >= 0) {
		sprintf(bcmd, "%s", "hiu");
		write(fd, bcmd, strlen(bcmd));
		close(fd);
		printf("set stb demux source to hiu!\n");
		return 0;
	}
	return -1;
}

int eAMLTSMPEGDecoder::setStbSource(int source)
{
	int fd;
	char *path = "/sys/class/tsdemux/stb_source"; 
	char  bcmd[16];
	fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd >= 0) {
		sprintf(bcmd, "%d", source);
		write(fd, bcmd, strlen(bcmd));
		close(fd);
		printf("set stb source to %d!\n", source);
		return 0;
	}
	return -1;
}

int eAMLTSMPEGDecoder::parseParameter(const char *para, int para_num, int *result)
{
	char *endp;
	const char *startp = para;
	int *out = result;
	int len = 0, count = 0;

	if (!startp) {
		return 0;
	}

	len = strlen(startp);

	do {
		//filter space out
		while (startp && (isspace(*startp) || !isgraph(*startp)) && len) {
			startp++;
			len--;
		}

		if (len == 0) {
			break;
		}

		*out++ = strtol(startp, &endp, 0);

		len -= endp - startp;
		startp = endp;
		count++;

	} while ((endp) && (count < para_num) && (len > 0));

	return count;
}


RESULT eAMLTSMPEGDecoder::play()
{
	TRACE__

	char filename[32];
	snprintf(filename, sizeof(filename), "/dev/dvb/adapter%d/dvr%d", 0, 0);
	for (int tries = 1; tries < 5; ++tries)
	{
		m_pvr_fd =  ::open(filename, (O_RDONLY | O_NONBLOCK));
		if(m_pvr_fd < 0)
		{
			perror("DVR DEVICE");
			eDebug("%s() unable to open dvr device",__PRETTY_FUNCTION__);
		}
		else
		{
			char* fb1_path = "/sys/class/graphics/fb1/blank";
			osdBlank(fb1_path,0);
			m_codec.noblock = 0;
			m_codec.has_video = 1;
			m_codec.video_pid = m_vpid;
			eDebug("[eAMLTSMPEGDecoder::play] Video PID: %d",m_codec.video_pid);
			m_codec.has_audio = 1;
			m_codec.audio_pid = m_apid;
			m_codec.audio_channels = 2;
			m_codec.audio_samplerate = 48000;
			m_codec.audio_info.channels = 2;
			m_codec.audio_info.sample_rate = m_codec.audio_samplerate;
			m_codec.audio_info.valid = 0;
			m_codec.stream_type = STREAM_TYPE_TS;

			setStbSource(0);
						
			int ret = codec_init(&m_codec);
			if(ret != CODEC_ERROR_NONE)
			{
				eDebug("[eAMLTSMPEGDecoder::play] Amlogic CODEC codec_init failed  !!!!!");
			}
			else
			{
				eDebug("[eAMLTSMPEGDecoder::play] Amlogic CODEC codec_init success !!!!!");
				
#if 0				
				struct buf_status buf_stat;
				if(0 == codec_get_vbuf_state(&m_codec, &buf_stat))
				{
					eDebug("[eAMLTSMPEGDecoder::play] Video buffer information size: %d data_len: %d free_len: %d",
						buf_stat.size,
						buf_stat.data_len,
						buf_stat.free_len);
				}

				if(0 == codec_get_abuf_state(&m_codec, &buf_stat))
				{
					eDebug("[eAMLTSMPEGDecoder::play] Audio buffer information size: %d data_len: %d free_len: %d",
						buf_stat.size,
						buf_stat.data_len,
						buf_stat.free_len);
				}

				// make sure we are not stuck in pause (amcodec bug)
				ret = codec_resume(&m_codec);
				eDebug("[eAMLTSMPEGDecoder::play] codec_resume: %d",ret);
#if 0
				ret = codec_set_cntl_mode(&m_codec, 0x00);
				eDebug("[eAMLTSMPEGDecoder::play] codec_set_cntl_mode: %d",ret);
#define PTS_FREQ    90000
#define AV_SYNC_THRESH    PTS_FREQ*30
				ret = codec_set_cntl_avthresh(&m_codec,AV_SYNC_THRESH);
				eDebug("[eAMLTSMPEGDecoder::play] codec_set_cntl_avthresh: %d",ret);
				ret = codec_set_cntl_syncthresh(&m_codec, 0);
				eDebug("[eAMLTSMPEGDecoder::play] codec_set_cntl_syncthresh: %d",ret);
				setAvsyncEnable(0);
				setAvsyncEnable(1);
#endif
#endif
				setAvsyncEnable(1);
			}
			m_threadDecoder.start(m_pvr_fd,&m_codec);
			break;
		}
		usleep(50000);
	}
	return 0;
}

RESULT eAMLTSMPEGDecoder::pause()
{
	TRACE__
	return 0;
}

RESULT eAMLTSMPEGDecoder::setFastForward(int frames_to_skip)
{
	TRACE__
	// fast forward is only possible if video data is present
	return 0;
}

RESULT eAMLTSMPEGDecoder::setSlowMotion(int repeat)
{
	TRACE__
	// slow motion is only possible if video data is present
	return 0;
}

RESULT eAMLTSMPEGDecoder::setTrickmode()
{
	TRACE__
	return 0;
}

RESULT eAMLTSMPEGDecoder::flush()
{
	TRACE__
	return 0;
}

void eAMLTSMPEGDecoder::demux_event(int event)
{
	TRACE__
	switch (event)
	{
	case eDVBDemux::evtFlush:
		flush();
		break;
	default:
		break;
	}
}

RESULT eAMLTSMPEGDecoder::getPTS(int what, pts_t &pts)
{
	TRACE__
	return 0;
}

RESULT eAMLTSMPEGDecoder::setRadioPic(const std::string &filename)
{
	TRACE__
	m_radio_pic = filename;
	return 0;
}

RESULT eAMLTSMPEGDecoder::showSinglePic(const char *filename)
{
	TRACE__
	return 0;
}

void eAMLTSMPEGDecoder::finishShowSinglePic()
{
	TRACE__
}

RESULT eAMLTSMPEGDecoder::connectVideoEvent(const Slot1<void, struct videoEvent> &event, ePtr<eConnection> &conn)
{
	TRACE__
	conn = new eConnection(this, m_video_event.connect(event));
	return 0;
}

void eAMLTSMPEGDecoder::video_event(struct videoEvent event)
{
	TRACE__
	/* emit */ m_video_event(event);
}

int eAMLTSMPEGDecoder::getVideoWidth()
{
	TRACE__
	return 0;
}

int eAMLTSMPEGDecoder::getVideoHeight()
{
	TRACE__
	return 0;
}

int eAMLTSMPEGDecoder::getVideoProgressive()
{
	TRACE__
	return 0;
}

int eAMLTSMPEGDecoder::getVideoFrameRate()
{
	TRACE__
	return 0;
}

int eAMLTSMPEGDecoder::getVideoAspect()
{
	TRACE__
	return 0;
}
