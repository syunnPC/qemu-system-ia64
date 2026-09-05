/*
 * Cirrus Logic CS4281 PCI audio controller
 *
 * Models selected PCI and BA0/BA1 registers, primary AC '97 playback and
 * capture on all four DMA channels, and a subset of a CS4297A codec.  Legacy
 * audio interfaces, FM synthesis, game port, MIDI data, a secondary codec,
 * and non-PCM serial slots are not implemented.
 * Technical references are listed in
 * docs/devel/device-emulation-provenance.rst.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/audio/cs4281.h"
#include "hw/audio/model.h"
#include "migration/vmstate.h"
#include "qemu/audio.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "trace.h"

#include "ac97.h"

#define CS4281_REG_WORDS (CS4281_BA0_SIZE / sizeof(uint32_t))
#define CS4281_CODEC_REGS 64
#define CS4281_DMA_CHANNELS 4

#define BA0_HISR       0x0000
#define BA0_HICR       0x0008
#define BA0_HIMR       0x000c
#define BA0_HDSR0      0x00f0
#define BA0_DCA0       0x0110
#define BA0_DCC0       0x0114
#define BA0_DBA0       0x0118
#define BA0_DBC0       0x011c
#define BA0_DMR0       0x0150
#define BA0_DCR0       0x0154
#define BA0_FCR0       0x0180
#define BA0_FSIC0      0x0210
#define BA0_EPPMC      0x03e4
#define BA0_CWPR       0x03e0
#define BA0_GPIOR      0x03e8
#define BA0_SPMC       0x03ec
#define BA0_CFLR       0x03f0
#define BA0_IISR       0x03f4
#define BA0_SSVID      0x03fc
#define BA0_CLKCR1     0x0400
#define BA0_SERMC      0x0420
#define BA0_SERC1      0x0428
#define BA0_SERC2      0x042c
#define BA0_ACCTL      0x0460
#define BA0_ACSTS      0x0464
#define BA0_ACOSV      0x0468
#define BA0_ACCAD      0x046c
#define BA0_ACCDA      0x0470
#define BA0_ACISV      0x0474
#define BA0_ACSAD      0x0478
#define BA0_ACSDA      0x047c
#define BA0_MIDCR      0x0490
#define BA0_MIDSR      0x0494
#define BA0_ACSTS2     0x04e4
#define BA0_ACISV2     0x04f4
#define BA0_ACSAD2     0x04f8
#define BA0_ACSDA2     0x04fc
#define BA0_FMSR       0x0730
#define BA0_SSPM       0x0740
#define BA0_DACSR      0x0744
#define BA0_ADCSR      0x0748
#define BA0_SRCSA      0x075c
#define BA0_PPLVC      0x0760
#define BA0_PPRVC      0x0764

#define HISR_INTENA    BIT(31)
#define HISR_MIDI      BIT(22)
#define HISR_DMAI      BIT(18)
#define HISR_DMA(c)    BIT(8 + (c))
#define HISR_DMA_MASK  (HISR_DMA(0) | HISR_DMA(1) | \
                        HISR_DMA(2) | HISR_DMA(3))

#define HICR_CHGM      BIT(1)
#define HICR_IEV       BIT(0)

#define HDSR_DHTC      BIT(17)
#define HDSR_DTC       BIT(16)
#define HDSR_DRUN      BIT(15)
#define HDSR_RQ        BIT(7)

#define DMR_DMA        BIT(29)
#define DMR_CBC        BIT(24)
#define DMR_SIZE20     BIT(20)
#define DMR_USIGN      BIT(19)
#define DMR_BEND       BIT(18)
#define DMR_MONO       BIT(17)
#define DMR_SIZE8      BIT(16)
#define DMR_DEC        BIT(5)
#define DMR_AUTO       BIT(4)
#define DMR_TR_MASK    (3U << 2)
#define DMR_TR_WRITE   (1U << 2)
#define DMR_TR_READ    (2U << 2)

#define DCR_HTCIE      BIT(17)
#define DCR_TCIE       BIT(16)
#define DCR_MSK        BIT(0)

#define FCR_FEN        BIT(31)
#define FCR_LS(v)      (((v) >> 16) & 0x1f)
#define FCR_RS(v)      (((v) >> 24) & 0x1f)
#define SLOT_PCM_OUT_L 0
#define SLOT_PCM_OUT_R 1
#define SLOT_PCM_IN_L  10
#define SLOT_PCM_IN_R  11
#define SLOT_DISABLED 31

#define EPPMC_FPDN     BIT(14)
#define SPMC_RSTN      BIT(0)

#define CLKCR1_CLKON   BIT(25)
#define CLKCR1_DLLRDY  BIT(24)
#define CLKCR1_SWCE    BIT(5)
#define CLKCR1_DLLP    BIT(4)

#define SERMC_PTC_MASK (7U << 1)
#define SERMC_PTC_AC97 BIT(1)
#define SERMC_MSPE     BIT(0)
#define SERMC_RO_MASK  (SERMC_PTC_MASK | SERMC_MSPE)
#define SERMC_DEFAULT  0x00010003U

#define ACCTL_TC       BIT(6)
#define ACCTL_CRW      BIT(4)
#define ACCTL_DCV      BIT(3)
#define ACCTL_VFRM     BIT(2)
#define ACCTL_ESYN     BIT(1)
#define ACSTS_VSTS     BIT(1)
#define ACSTS_CRDY     BIT(0)
#define ACISV_SLOTS_3_4 (BIT(0) | BIT(1))

#define SSPM_MIXEN     BIT(6)
#define SSPM_CSRCEN    BIT(5)
#define SSPM_PSRCEN    BIT(4)
#define SSPM_ACLEN     BIT(2)

#define MIDCR_MRST     BIT(5)
#define MIDSR_RBE      BIT(7)

#define CS4281_PM_CAP  0x40
#define CS4281_CFG_CWPR  0xe0
#define CS4281_CFG_GPIOR 0xe8
#define CS4281_CFG_SPMC  0xec
#define CS4281_CFG_CFLR  0xf0
#define CS4281_CFG_IISR  0xf4
#define CS4281_CFG_SSVID 0xfc

typedef struct CS4281DMAState {
    uint32_t hdsr;
    bool half_fired;
} CS4281DMAState;

struct CS4281State {
    PCIDevice parent_obj;

    MemoryRegion ba0;
    MemoryRegion ba1;
    AudioBackend *audio_be;
    SWVoiceOut *voice_out;
    SWVoiceIn *voice_in;

    uint32_t regs[CS4281_REG_WORDS];
    uint8_t fifo[CS4281_BA1_SIZE];
    uint16_t codec_regs[CS4281_CODEC_REGS];
    CS4281DMAState dma[CS4281_DMA_CHANNELS];
    uint32_t hisr_pending;
    bool irq_enabled;

    struct audsettings out_settings;
    struct audsettings in_settings;
    bool out_settings_valid;
    bool in_settings_valid;
    /* Active channels follow the DMA direction and codec FIFO slot routing. */
    unsigned playback_channel;
    unsigned capture_channel;
    QEMUBH *streams_bh;
};

#define CS_REG(s, reg) ((s)->regs[(reg) >> 2])
#define CODEC_REG(s, reg) ((s)->codec_regs[((reg) & 0x7e) >> 1])

static void cs4281_update_streams(CS4281State *s, bool force);

static void cs4281_update_clock(CS4281State *s)
{
    uint32_t clkcr1 = CS_REG(s, BA0_CLKCR1);
    bool clock_on = (CS_REG(s, BA0_SPMC) & SPMC_RSTN) &&
                    !(CS_REG(s, BA0_EPPMC) & EPPMC_FPDN);

    clkcr1 &= ~(CLKCR1_CLKON | CLKCR1_DLLRDY);
    if (clock_on) {
        clkcr1 |= CLKCR1_CLKON;
        if (clkcr1 & CLKCR1_DLLP) {
            clkcr1 |= CLKCR1_DLLRDY;
        }
    }
    CS_REG(s, BA0_CLKCR1) = clkcr1;
}

static bool cs4281_core_clock_enabled(CS4281State *s)
{
    uint32_t clkcr1 = CS_REG(s, BA0_CLKCR1);

    return (clkcr1 & (CLKCR1_DLLRDY | CLKCR1_SWCE)) ==
           (CLKCR1_DLLRDY | CLKCR1_SWCE);
}

static bool cs4281_codec_ready(CS4281State *s)
{
    uint32_t sermc = CS_REG(s, BA0_SERMC);

    return (CS_REG(s, BA0_SPMC) & SPMC_RSTN) &&
           !(CS_REG(s, BA0_EPPMC) & EPPMC_FPDN) &&
           (CS_REG(s, BA0_SSPM) & SSPM_ACLEN) &&
           (sermc & SERMC_MSPE) &&
           (sermc & SERMC_PTC_MASK) == SERMC_PTC_AC97 &&
           cs4281_core_clock_enabled(s) &&
           (CS_REG(s, BA0_ACCTL) & ACCTL_ESYN);
}

static void cs4281_update_irq(CS4281State *s)
{
    uint32_t active = s->hisr_pending & ~CS_REG(s, BA0_HIMR);

    pci_set_irq(&s->parent_obj, s->irq_enabled && active);
}

static void cs4281_update_link(CS4281State *s)
{
    uint32_t status = CS_REG(s, BA0_ACSTS) & ACSTS_VSTS;
    bool ready = cs4281_codec_ready(s);

    if (ready) {
        status |= ACSTS_CRDY;
    }
    CS_REG(s, BA0_ACSTS) = status;
    CS_REG(s, BA0_ACISV) = ready &&
        (CS_REG(s, BA0_ACCTL) & ACCTL_VFRM) ? ACISV_SLOTS_3_4 : 0;
    CS_REG(s, BA0_ACSTS2) = 0;
    CS_REG(s, BA0_ACISV2) = 0;
}

static uint8_t cs4281_volume(uint16_t value, unsigned shift,
                             unsigned mask)
{
    unsigned attenuation = (value >> shift) & mask;

    return 255 - (attenuation * 255 / mask);
}

static void cs4281_codec_update_volume(CS4281State *s)
{
    uint16_t master = CODEC_REG(s, AC97_Master_Volume_Mute);
    uint16_t pcm = CODEC_REG(s, AC97_PCM_Out_Volume_Mute);
    bool mute = (master | pcm) & BIT(15);
    unsigned left, right;

    if (!s->voice_out) {
        return;
    }
    left = cs4281_volume(master, 8, 0x3f) *
           cs4281_volume(pcm, 8, 0x1f) / 255;
    right = cs4281_volume(master, 0, 0x3f) *
            cs4281_volume(pcm, 0, 0x1f) / 255;
    audio_be_set_volume_out_lr(s->audio_be, s->voice_out,
                               mute, left, right);
}

static void cs4281_codec_reset(CS4281State *s)
{
    memset(s->codec_regs, 0, sizeof(s->codec_regs));
    CODEC_REG(s, AC97_Reset) = 0x1990;
    CODEC_REG(s, AC97_Master_Volume_Mute) = 0x8000;
    CODEC_REG(s, AC97_Headphone_Volume_Mute) = 0x8000;
    CODEC_REG(s, AC97_Master_Volume_Mono_Mute) = 0x8000;
    CODEC_REG(s, AC97_PC_BEEP_Volume_Mute) = 0x0000;
    CODEC_REG(s, AC97_Phone_Volume_Mute) = 0x8008;
    CODEC_REG(s, AC97_Mic_Volume_Mute) = 0x8008;
    CODEC_REG(s, AC97_Line_In_Volume_Mute) = 0x8808;
    CODEC_REG(s, AC97_CD_Volume_Mute) = 0x8808;
    CODEC_REG(s, AC97_Video_Volume_Mute) = 0x8808;
    CODEC_REG(s, AC97_Aux_Volume_Mute) = 0x8808;
    CODEC_REG(s, AC97_PCM_Out_Volume_Mute) = 0x8808;
    CODEC_REG(s, AC97_Record_Select) = 0x0000;
    CODEC_REG(s, AC97_Record_Gain_Mute) = 0x8000;
    CODEC_REG(s, AC97_Powerdown_Ctrl_Stat) = 0x000f;
    CODEC_REG(s, AC97_Extended_Audio_ID) = 0x0200;
    CODEC_REG(s, AC97_PCM_Front_DAC_Rate) = 48000;
    CODEC_REG(s, AC97_PCM_LR_ADC_Rate) = 48000;
    CODEC_REG(s, 0x5e) = 0x0080;
    CODEC_REG(s, 0x60) = 0x0023;
    CODEC_REG(s, AC97_Vendor_ID1) = 0x4352;
    CODEC_REG(s, AC97_Vendor_ID2) = 0x5911;
    cs4281_codec_update_volume(s);
}

static uint16_t cs4281_codec_read(CS4281State *s, uint8_t reg)
{
    return CODEC_REG(s, reg);
}

static void cs4281_codec_write(CS4281State *s, uint8_t reg, uint16_t value)
{
    reg &= 0x7e;
    switch (reg) {
    case AC97_Reset:
        cs4281_codec_reset(s);
        return;
    case AC97_Master_Volume_Mute:
    case AC97_Headphone_Volume_Mute:
        value &= 0xbf3f;
        break;
    case AC97_Master_Volume_Mono_Mute:
        value &= 0x803f;
        break;
    case AC97_PC_BEEP_Volume_Mute:
        value &= 0x801e;
        break;
    case AC97_Phone_Volume_Mute:
        value &= 0x801f;
        break;
    case AC97_Mic_Volume_Mute:
        value &= 0x805f;
        break;
    case AC97_Line_In_Volume_Mute:
    case AC97_CD_Volume_Mute:
    case AC97_Video_Volume_Mute:
    case AC97_Aux_Volume_Mute:
    case AC97_PCM_Out_Volume_Mute:
        value &= 0x9f1f;
        break;
    case AC97_Record_Select:
        value &= 0x0707;
        break;
    case AC97_Record_Gain_Mute:
    case AC97_Record_Gain_Mic_Mute:
        value &= 0x8f0f;
        break;
    case AC97_Powerdown_Ctrl_Stat:
        value = (value & 0xff00) | 0x000f;
        break;
    case AC97_Extended_Audio_ID:
    case AC97_PCM_Front_DAC_Rate:
    case AC97_PCM_LR_ADC_Rate:
    case AC97_Vendor_ID1:
    case AC97_Vendor_ID2:
        return;
    default:
        break;
    }
    CODEC_REG(s, reg) = value;
    if (reg == AC97_Master_Volume_Mute ||
        reg == AC97_PCM_Out_Volume_Mute) {
        cs4281_codec_update_volume(s);
    }
}

static void cs4281_ac97_command(CS4281State *s)
{
    uint32_t acctl = CS_REG(s, BA0_ACCTL);
    uint8_t reg;

    if (!(acctl & ACCTL_DCV) || !cs4281_codec_ready(s) ||
        !(acctl & ACCTL_VFRM)) {
        return;
    }

    /* A command to an absent secondary codec still leaves the command FIFO. */
    if (acctl & ACCTL_TC) {
        CS_REG(s, BA0_ACCTL) &= ~ACCTL_DCV;
        return;
    }

    reg = CS_REG(s, BA0_ACCAD) & 0x7e;
    if (acctl & ACCTL_CRW) {
        CS_REG(s, BA0_ACSAD) = reg;
        CS_REG(s, BA0_ACSDA) = cs4281_codec_read(s, reg);
        CS_REG(s, BA0_ACSTS) |= ACSTS_VSTS;
    } else {
        cs4281_codec_write(s, reg, CS_REG(s, BA0_ACCDA));
    }
    CS_REG(s, BA0_ACCTL) &= ~ACCTL_DCV;
}

static unsigned cs4281_dma_channel(hwaddr addr, hwaddr base,
                                   unsigned stride)
{
    return (addr - base) / stride;
}

static bool cs4281_dma_running(CS4281State *s, unsigned channel)
{
    uint32_t dmr = CS_REG(s, BA0_DMR0 + channel * 8);
    uint32_t dcr = CS_REG(s, BA0_DCR0 + channel * 8);
    uint32_t fcr = CS_REG(s, BA0_FCR0 + channel * 4);

    return (dmr & DMR_DMA) && !(dcr & DCR_MSK) && (fcr & FCR_FEN);
}

static unsigned cs4281_rate(uint32_t value)
{
    static const unsigned fixed[] = {
        48000, 44100, 22050, 16000, 11025, 8000,
    };

    if (value < ARRAY_SIZE(fixed)) {
        return fixed[value];
    }
    return MAX(4000U, MIN(48000U, 1536000U / value));
}

static struct audsettings cs4281_dma_settings(CS4281State *s,
                                               unsigned channel)
{
    uint32_t dmr = CS_REG(s, BA0_DMR0 + channel * 8);
    uint32_t fcr = CS_REG(s, BA0_FCR0 + channel * 4);
    bool capture = (dmr & DMR_TR_MASK) == DMR_TR_WRITE;
    uint32_t srcsa = CS_REG(s, BA0_SRCSA) >> (capture ? 16 : 0);
    bool src = (srcsa & 0x1f) == FCR_LS(fcr) &&
               ((dmr & DMR_MONO) || ((srcsa >> 8) & 0x1f) == FCR_RS(fcr));
    AudioFormat format;

    if (dmr & DMR_SIZE8) {
        format = dmr & DMR_USIGN ? AUDIO_FORMAT_U8 : AUDIO_FORMAT_S8;
    } else if (dmr & DMR_SIZE20) {
        format = dmr & DMR_USIGN ? AUDIO_FORMAT_U32 : AUDIO_FORMAT_S32;
    } else {
        format = dmr & DMR_USIGN ? AUDIO_FORMAT_U16 : AUDIO_FORMAT_S16;
    }

    return (struct audsettings) {
        .freq = src ? cs4281_rate(CS_REG(s, capture ? BA0_ADCSR : BA0_DACSR)) :
                      48000,
        .nchannels = dmr & DMR_MONO ? 1 : 2,
        .fmt = format,
        .big_endian = dmr & DMR_BEND,
    };
}

static bool cs4281_settings_equal(const struct audsettings *a,
                                  const struct audsettings *b)
{
    return a->freq == b->freq && a->nchannels == b->nchannels &&
           a->fmt == b->fmt && a->big_endian == b->big_endian;
}

static void cs4281_playback_callback(void *opaque, int free);
static void cs4281_capture_callback(void *opaque, int avail);

static void cs4281_open_voice(CS4281State *s, unsigned channel, bool capture,
                              bool force)
{
    struct audsettings settings = cs4281_dma_settings(s, channel);

    if (!capture) {
        if (force || !s->out_settings_valid ||
            !cs4281_settings_equal(&settings, &s->out_settings)) {
            s->voice_out = audio_be_open_out(s->audio_be, s->voice_out,
                                             "cs4281.playback", s,
                                             cs4281_playback_callback,
                                             &settings);
            s->out_settings = settings;
            s->out_settings_valid = true;
            cs4281_codec_update_volume(s);
        }
    } else {
        if (force || !s->in_settings_valid ||
            !cs4281_settings_equal(&settings, &s->in_settings)) {
            s->voice_in = audio_be_open_in(s->audio_be, s->voice_in,
                                           "cs4281.capture", s,
                                           cs4281_capture_callback,
                                           &settings);
            s->in_settings = settings;
            s->in_settings_valid = true;
        }
    }
}

static bool cs4281_stream_active(CS4281State *s, unsigned channel)
{
    uint32_t dmr;
    uint32_t fcr;
    bool routed;

    if (channel >= CS4281_DMA_CHANNELS) {
        return false;
    }
    dmr = CS_REG(s, BA0_DMR0 + channel * 8);
    fcr = CS_REG(s, BA0_FCR0 + channel * 4);
    switch (dmr & DMR_TR_MASK) {
    case DMR_TR_READ:
        routed = FCR_LS(fcr) == SLOT_PCM_OUT_L &&
                 (FCR_RS(fcr) == SLOT_PCM_OUT_R ||
                  ((dmr & DMR_MONO) && FCR_RS(fcr) == SLOT_DISABLED));
        break;
    case DMR_TR_WRITE:
        routed = FCR_LS(fcr) == SLOT_PCM_IN_L &&
                 (FCR_RS(fcr) == SLOT_PCM_IN_R ||
                  ((dmr & DMR_MONO) && FCR_RS(fcr) == SLOT_DISABLED));
        break;
    default:
        return false;
    }

    return routed && cs4281_codec_ready(s) &&
           (CS_REG(s, BA0_ACCTL) & ACCTL_VFRM) &&
           cs4281_dma_running(s, channel);
}

static void cs4281_update_streams(CS4281State *s, bool force)
{
    unsigned channel;

    s->playback_channel = CS4281_DMA_CHANNELS;
    s->capture_channel = CS4281_DMA_CHANNELS;
    for (channel = 0; channel < CS4281_DMA_CHANNELS; channel++) {
        bool capture;
        unsigned *selected;

        if (!cs4281_stream_active(s, channel)) {
            continue;
        }
        capture = (CS_REG(s, BA0_DMR0 + channel * 8) & DMR_TR_MASK) ==
                  DMR_TR_WRITE;
        selected = capture ? &s->capture_channel : &s->playback_channel;
        if (*selected == CS4281_DMA_CHANNELS) {
            *selected = channel;
            cs4281_open_voice(s, channel, capture, force);
        }
    }
    if (s->voice_out) {
        audio_be_set_active_out(s->audio_be, s->voice_out,
                                s->playback_channel < CS4281_DMA_CHANNELS);
    }
    if (s->voice_in) {
        audio_be_set_active_in(s->audio_be, s->voice_in,
                               s->capture_channel < CS4281_DMA_CHANNELS);
    }
}

static void cs4281_update_streams_bh(void *opaque)
{
    CS4281State *s = opaque;

    cs4281_update_streams(s, false);
}

static void cs4281_dma_event(CS4281State *s, unsigned channel,
                             uint32_t status)
{
    uint32_t dcr = CS_REG(s, BA0_DCR0 + channel * 8);

    s->dma[channel].hdsr |= status;
    if ((status == HDSR_DHTC && (dcr & DCR_HTCIE)) ||
        (status == HDSR_DTC && (dcr & DCR_TCIE))) {
        s->hisr_pending |= HISR_DMAI | HISR_DMA(channel);
        cs4281_update_irq(s);
    }
}

static unsigned cs4281_frame_bytes(CS4281State *s, unsigned channel)
{
    struct audsettings settings = cs4281_dma_settings(s, channel);

    return settings.nchannels * audio_format_bits(settings.fmt) / 8;
}

static void cs4281_dma_advance(CS4281State *s, unsigned channel,
                               unsigned frames, unsigned frame_bytes)
{
    uint32_t dmr_reg = BA0_DMR0 + channel * 8;
    uint32_t dca_reg = BA0_DCA0 + channel * 0x10;
    uint32_t dcc_reg = BA0_DCC0 + channel * 0x10;
    uint32_t dba_reg = BA0_DBA0 + channel * 0x10;
    uint32_t dbc_reg = BA0_DBC0 + channel * 0x10;
    uint32_t dmr = CS_REG(s, dmr_reg);
    uint32_t old_count = CS_REG(s, dcc_reg);
    uint32_t base_count = CS_REG(s, dbc_reg);
    uint32_t half_count = base_count / 2;
    uint32_t bytes = frames * frame_bytes;

    if (dmr & DMR_DEC) {
        CS_REG(s, dca_reg) -= bytes;
    } else {
        CS_REG(s, dca_reg) += bytes;
    }

    if (frames > old_count) {
        cs4281_dma_event(s, channel, HDSR_DTC);
        if (dmr & DMR_AUTO) {
            CS_REG(s, dca_reg) = CS_REG(s, dba_reg);
            CS_REG(s, dcc_reg) = base_count;
            s->dma[channel].half_fired = false;
        } else {
            CS_REG(s, dcc_reg) = 0;
            CS_REG(s, dmr_reg) &= ~DMR_DMA;
        }
        return;
    }

    CS_REG(s, dcc_reg) = old_count - frames;
    if (!s->dma[channel].half_fired && old_count > half_count &&
        CS_REG(s, dcc_reg) <= half_count) {
        s->dma[channel].half_fired = true;
        cs4281_dma_event(s, channel, HDSR_DHTC);
    }
}

static size_t cs4281_dma_limit(CS4281State *s, unsigned channel,
                               size_t bytes, unsigned frame_bytes)
{
    uint32_t count = CS_REG(s, BA0_DCC0 + channel * 0x10);
    uint32_t base_count = CS_REG(s, BA0_DBC0 + channel * 0x10);
    size_t frames = bytes / frame_bytes;

    frames = MIN(frames, (size_t)count + 1);
    if (!s->dma[channel].half_fired && count > base_count / 2) {
        frames = MIN(frames, (size_t)(count - base_count / 2));
    }
    return frames * frame_bytes;
}

static void cs4281_dma_error(CS4281State *s, unsigned channel)
{
    qemu_log_mask(LOG_GUEST_ERROR,
                  "cs4281: channel %u PCI DMA transaction failed\n", channel);
    CS_REG(s, BA0_DCR0 + channel * 8) |= DCR_MSK;
    qemu_bh_schedule(s->streams_bh);
}

static void cs4281_playback_callback(void *opaque, int free)
{
    CS4281State *s = opaque;
    unsigned channel = s->playback_channel;
    uint8_t buffer[4096];
    unsigned frame_bytes;

    if (!cs4281_stream_active(s, channel)) {
        return;
    }
    frame_bytes = cs4281_frame_bytes(s, channel);
    while (free >= frame_bytes && cs4281_stream_active(s, channel)) {
        uint32_t dmr = CS_REG(s, BA0_DMR0 + channel * 8);
        uint32_t address = CS_REG(s, BA0_DCA0 + channel * 0x10);
        size_t amount = MIN((size_t)free, sizeof(buffer));
        size_t copied;

        amount = cs4281_dma_limit(s, channel, amount, frame_bytes);
        if (dmr & DMR_DEC) {
            amount = MIN(amount, (size_t)frame_bytes);
        }
        if (!amount) {
            break;
        }
        if (pci_dma_read(&s->parent_obj, address, buffer, amount) != MEMTX_OK) {
            cs4281_dma_error(s, channel);
            break;
        }
        copied = audio_be_write(s->audio_be, s->voice_out, buffer, amount);
        copied -= copied % frame_bytes;
        if (!copied) {
            break;
        }
        cs4281_dma_advance(s, channel, copied / frame_bytes, frame_bytes);
        free -= copied;
    }
    /* Defer voice reconfiguration until after the audio callback returns. */
    qemu_bh_schedule(s->streams_bh);
}

static void cs4281_capture_callback(void *opaque, int avail)
{
    CS4281State *s = opaque;
    unsigned channel = s->capture_channel;
    uint8_t buffer[4096];
    unsigned frame_bytes;

    if (!cs4281_stream_active(s, channel)) {
        return;
    }
    frame_bytes = cs4281_frame_bytes(s, channel);
    while (avail >= frame_bytes && cs4281_stream_active(s, channel)) {
        uint32_t dmr = CS_REG(s, BA0_DMR0 + channel * 8);
        uint32_t address = CS_REG(s, BA0_DCA0 + channel * 0x10);
        size_t amount = MIN((size_t)avail, sizeof(buffer));
        size_t acquired;

        amount = cs4281_dma_limit(s, channel, amount, frame_bytes);
        if (dmr & DMR_DEC) {
            amount = MIN(amount, (size_t)frame_bytes);
        }
        if (!amount) {
            break;
        }
        acquired = audio_be_read(s->audio_be, s->voice_in, buffer, amount);
        acquired -= acquired % frame_bytes;
        if (!acquired) {
            break;
        }
        if (pci_dma_write(&s->parent_obj, address, buffer, acquired) !=
            MEMTX_OK) {
            cs4281_dma_error(s, channel);
            break;
        }
        cs4281_dma_advance(s, channel, acquired / frame_bytes, frame_bytes);
        avail -= acquired;
    }
    qemu_bh_schedule(s->streams_bh);
}

static uint32_t cs4281_hdsr_read(CS4281State *s, unsigned channel)
{
    uint32_t value = s->dma[channel].hdsr;

    if (cs4281_dma_running(s, channel)) {
        value |= HDSR_DRUN | HDSR_RQ;
    }
    s->dma[channel].hdsr &= ~(HDSR_DHTC | HDSR_DTC);
    s->hisr_pending &= ~HISR_DMA(channel);
    if (!(s->hisr_pending & HISR_DMA_MASK)) {
        s->hisr_pending &= ~HISR_DMAI;
    }
    cs4281_update_irq(s);
    return value;
}

static uint64_t cs4281_ba0_read(void *opaque, hwaddr addr, unsigned size)
{
    CS4281State *s = opaque;
    uint32_t value;

    if (addr >= CS4281_BA0_SIZE) {
        value = UINT32_MAX;
    } else if (addr >= BA0_HDSR0 && addr < BA0_HDSR0 + 4 * 4) {
        value = cs4281_hdsr_read(s,
            cs4281_dma_channel(addr, BA0_HDSR0, 4));
    } else {
        switch (addr) {
        case BA0_HISR:
            value = s->hisr_pending |
                    (s->irq_enabled ? HISR_INTENA : 0);
            break;
        case BA0_ACSDA:
            value = CS_REG(s, BA0_ACSDA);
            CS_REG(s, BA0_ACSTS) &= ~ACSTS_VSTS;
            break;
        case BA0_ACSDA2:
        case BA0_FMSR:
            value = 0;
            break;
        case BA0_MIDSR:
            value = MIDSR_RBE;
            break;
        default:
            value = CS_REG(s, addr);
            break;
        }
    }
    trace_cs4281_reg_access(0, false, addr, value, size);
    return value;
}

static void cs4281_sync_vendor_config(CS4281State *s)
{
    PCIDevice *pdev = &s->parent_obj;

    pci_set_long(pdev->config + CS4281_CFG_CWPR, CS_REG(s, BA0_CWPR));
    pci_set_long(pdev->config + CS4281_CFG_GPIOR, CS_REG(s, BA0_GPIOR));
    pci_set_long(pdev->config + CS4281_CFG_SPMC, CS_REG(s, BA0_SPMC));
    pci_set_long(pdev->config + CS4281_CFG_CFLR, CS_REG(s, BA0_CFLR));
    pci_set_long(pdev->config + CS4281_CFG_IISR, CS_REG(s, BA0_IISR));
    pci_set_long(pdev->config + CS4281_CFG_SSVID, CS_REG(s, BA0_SSVID));
}

static bool cs4281_vendor_unlocked(CS4281State *s)
{
    return (CS_REG(s, BA0_CWPR) & 0xffff) == 0x4281;
}

static void cs4281_vendor_write(CS4281State *s, hwaddr addr, uint32_t value)
{
    if (addr != BA0_CWPR && !cs4281_vendor_unlocked(s)) {
        return;
    }
    CS_REG(s, addr) = value;
    if (addr == BA0_SSVID) {
        pci_set_long(s->parent_obj.config + PCI_SUBSYSTEM_VENDOR_ID, value);
    }
    cs4281_sync_vendor_config(s);
}

static void cs4281_ba0_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned size)
{
    CS4281State *s = opaque;
    uint32_t old;
    unsigned channel;

    if (addr >= CS4281_BA0_SIZE) {
        return;
    }
    value = (uint32_t)value;
    trace_cs4281_reg_access(0, true, addr, value, size);

    if (addr >= BA0_DCA0 && addr < BA0_DCA0 + 4 * 0x10) {
        channel = cs4281_dma_channel(addr, BA0_DCA0, 0x10);
        switch ((addr - BA0_DCA0) & 0xf) {
        case 0x8:
            CS_REG(s, addr) = value;
            if (!cs4281_dma_running(s, channel)) {
                CS_REG(s, BA0_DCA0 + channel * 0x10) = value;
            }
            return;
        case 0xc:
            CS_REG(s, addr) = value;
            if (!cs4281_dma_running(s, channel)) {
                CS_REG(s, BA0_DCC0 + channel * 0x10) = value;
                s->dma[channel].half_fired = false;
            }
            return;
        default:
            return;
        }
    }

    if (addr >= BA0_DMR0 && addr < BA0_DMR0 + 4 * 8) {
        channel = cs4281_dma_channel(addr, BA0_DMR0, 8);
        if (((addr - BA0_DMR0) & 7) == 0) {
            old = CS_REG(s, addr);
            CS_REG(s, addr) = value;
            if (!(old & DMR_DMA) && (value & DMR_DMA)) {
                CS_REG(s, BA0_DCA0 + channel * 0x10) =
                    CS_REG(s, BA0_DBA0 + channel * 0x10);
                CS_REG(s, BA0_DCC0 + channel * 0x10) =
                    CS_REG(s, BA0_DBC0 + channel * 0x10);
                s->dma[channel].half_fired = false;
            }
        } else {
            CS_REG(s, addr) = value & (DCR_HTCIE | DCR_TCIE | DCR_MSK);
        }
        cs4281_update_streams(s, false);
        return;
    }

    if (addr >= BA0_FCR0 && addr < BA0_FCR0 + 4 * 4) {
        CS_REG(s, addr) = value;
        cs4281_update_streams(s, false);
        return;
    }
    if (addr >= BA0_FSIC0 && addr < BA0_FSIC0 + 4 * 4) {
        CS_REG(s, addr) = value;
        return;
    }

    switch (addr) {
    case BA0_HISR:
        return;
    case BA0_HICR:
        if (value & HICR_CHGM) {
            s->irq_enabled = value & HICR_IEV;
        }
        CS_REG(s, BA0_HICR) = value & (HICR_CHGM | HICR_IEV);
        cs4281_update_irq(s);
        return;
    case BA0_HIMR:
        CS_REG(s, BA0_HIMR) = value & ~HISR_INTENA;
        cs4281_update_irq(s);
        return;
    case BA0_CWPR:
    case BA0_GPIOR:
    case BA0_SPMC:
    case BA0_CFLR:
    case BA0_IISR:
    case BA0_SSVID:
        old = CS_REG(s, addr);
        cs4281_vendor_write(s, addr, value);
        if (addr == BA0_SPMC) {
            if ((old & SPMC_RSTN) && !(CS_REG(s, addr) & SPMC_RSTN)) {
                cs4281_codec_reset(s);
            }
            cs4281_update_clock(s);
            cs4281_update_link(s);
            cs4281_update_streams(s, false);
        }
        return;
    case BA0_CLKCR1:
        CS_REG(s, addr) = value & ~(CLKCR1_CLKON | CLKCR1_DLLRDY);
        cs4281_update_clock(s);
        cs4281_update_link(s);
        cs4281_update_streams(s, false);
        return;
    case BA0_SERMC:
        CS_REG(s, addr) = (value & ~SERMC_RO_MASK) |
                          (CS_REG(s, addr) & SERMC_RO_MASK);
        cs4281_update_link(s);
        cs4281_update_streams(s, false);
        return;
    case BA0_SSPM:
    case BA0_EPPMC:
        CS_REG(s, addr) = value;
        if (addr == BA0_EPPMC) {
            cs4281_update_clock(s);
        }
        cs4281_update_link(s);
        cs4281_update_streams(s, false);
        return;
    case BA0_ACCTL:
        CS_REG(s, addr) = value &
            (ACCTL_TC | ACCTL_CRW | ACCTL_DCV | ACCTL_VFRM | ACCTL_ESYN);
        cs4281_update_link(s);
        cs4281_ac97_command(s);
        cs4281_update_streams(s, false);
        return;
    case BA0_ACSTS:
    case BA0_ACISV:
    case BA0_ACSAD:
    case BA0_ACSDA:
    case BA0_ACSTS2:
    case BA0_ACISV2:
    case BA0_ACSAD2:
    case BA0_ACSDA2:
        return;
    case BA0_MIDCR:
        CS_REG(s, addr) = value & ~MIDCR_MRST;
        s->hisr_pending &= ~HISR_MIDI;
        cs4281_update_irq(s);
        return;
    case BA0_DACSR:
    case BA0_ADCSR:
    case BA0_SRCSA:
        CS_REG(s, addr) = value;
        cs4281_update_streams(s, false);
        return;
    default:
        CS_REG(s, addr) = value;
        return;
    }
}

static uint64_t cs4281_ba1_read(void *opaque, hwaddr addr, unsigned size)
{
    CS4281State *s = opaque;
    uint32_t value = ldl_le_p(&s->fifo[addr]);

    trace_cs4281_reg_access(1, false, addr, value, size);
    return value;
}

static void cs4281_ba1_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned size)
{
    CS4281State *s = opaque;

    trace_cs4281_reg_access(1, true, addr, value, size);
    stl_le_p(&s->fifo[addr], value);
}

static const MemoryRegionOps cs4281_ba0_ops = {
    .read = cs4281_ba0_read,
    .write = cs4281_ba0_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps cs4281_ba1_ops = {
    .read = cs4281_ba1_read,
    .write = cs4281_ba1_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void cs4281_config_write(PCIDevice *pdev, uint32_t address,
                                uint32_t value, int length)
{
    CS4281State *s = CS4281(pdev);
    uint8_t saved[0x20];
    bool unlocked = cs4281_vendor_unlocked(s);
    uint32_t old_spmc = CS_REG(s, BA0_SPMC);

    trace_cs4281_config_write(address, value, length);
    memcpy(saved, pdev->config + CS4281_CFG_CWPR, sizeof(saved));
    pci_default_write_config(pdev, address, value, length);

    if (ranges_overlap(address, length, CS4281_CFG_CWPR, sizeof(saved))) {
        if (!unlocked && !ranges_overlap(address, length,
                                         CS4281_CFG_CWPR, 4)) {
            memcpy(pdev->config + CS4281_CFG_CWPR, saved, sizeof(saved));
        }
        CS_REG(s, BA0_CWPR) = pci_get_long(pdev->config + CS4281_CFG_CWPR);
        if (cs4281_vendor_unlocked(s)) {
            CS_REG(s, BA0_GPIOR) =
                pci_get_long(pdev->config + CS4281_CFG_GPIOR);
            CS_REG(s, BA0_SPMC) =
                pci_get_long(pdev->config + CS4281_CFG_SPMC);
            CS_REG(s, BA0_CFLR) =
                pci_get_long(pdev->config + CS4281_CFG_CFLR);
            CS_REG(s, BA0_IISR) =
                pci_get_long(pdev->config + CS4281_CFG_IISR);
            CS_REG(s, BA0_SSVID) =
                pci_get_long(pdev->config + CS4281_CFG_SSVID);
            pci_set_long(pdev->config + PCI_SUBSYSTEM_VENDOR_ID,
                         CS_REG(s, BA0_SSVID));
        }
        if ((old_spmc & SPMC_RSTN) &&
            !(CS_REG(s, BA0_SPMC) & SPMC_RSTN)) {
            cs4281_codec_reset(s);
        }
        cs4281_update_clock(s);
        cs4281_update_link(s);
        cs4281_update_streams(s, false);
    }
}

static void cs4281_reset(DeviceState *dev)
{
    CS4281State *s = CS4281(dev);
    unsigned channel;

    qemu_bh_cancel(s->streams_bh);
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->fifo, 0, sizeof(s->fifo));
    memset(s->dma, 0, sizeof(s->dma));
    s->hisr_pending = 0;
    s->irq_enabled = false;

    CS_REG(s, BA0_HIMR) = 0x7fffffff;
    CS_REG(s, BA0_CFLR) = 1;
    CS_REG(s, BA0_SERMC) = SERMC_DEFAULT;
    CS_REG(s, BA0_SERC1) = 3;
    CS_REG(s, BA0_SERC2) = 3;
    CS_REG(s, BA0_DACSR) = 0;
    CS_REG(s, BA0_ADCSR) = 0;
    CS_REG(s, BA0_SSVID) =
        pci_get_long(s->parent_obj.config + PCI_SUBSYSTEM_VENDOR_ID);
    for (channel = 0; channel < CS4281_DMA_CHANNELS; channel++) {
        CS_REG(s, BA0_DCR0 + channel * 8) = DCR_MSK;
    }
    cs4281_codec_reset(s);
    cs4281_sync_vendor_config(s);
    cs4281_update_clock(s);
    cs4281_update_link(s);
    cs4281_update_streams(s, false);
    pci_irq_deassert(&s->parent_obj);
}

static int cs4281_post_load(void *opaque, int version_id)
{
    CS4281State *s = opaque;

    qemu_bh_cancel(s->streams_bh);
    cs4281_sync_vendor_config(s);
    cs4281_update_clock(s);
    cs4281_update_link(s);
    cs4281_update_streams(s, true);
    cs4281_update_irq(s);
    return 0;
}

static const VMStateDescription vmstate_cs4281_dma = {
    .name = "cs4281/dma",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(hdsr, CS4281DMAState),
        VMSTATE_BOOL(half_fired, CS4281DMAState),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_cs4281 = {
    .name = "cs4281",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = cs4281_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, CS4281State),
        VMSTATE_UINT32_ARRAY(regs, CS4281State, CS4281_REG_WORDS),
        VMSTATE_BUFFER(fifo, CS4281State),
        VMSTATE_UINT16_ARRAY(codec_regs, CS4281State, CS4281_CODEC_REGS),
        VMSTATE_STRUCT_ARRAY(dma, CS4281State, CS4281_DMA_CHANNELS, 1,
                             vmstate_cs4281_dma, CS4281DMAState),
        VMSTATE_UINT32(hisr_pending, CS4281State),
        VMSTATE_BOOL(irq_enabled, CS4281State),
        VMSTATE_END_OF_LIST()
    },
};

static void cs4281_realize(PCIDevice *pdev, Error **errp)
{
    CS4281State *s = CS4281(pdev);
    int pm_cap;

    if (!audio_be_check(&s->audio_be, errp)) {
        return;
    }

    pci_set_word(pdev->config + PCI_STATUS,
                 PCI_STATUS_CAP_LIST | PCI_STATUS_DEVSEL_MEDIUM);
    pdev->config[PCI_INTERRUPT_PIN] = 1;
    pdev->config[PCI_MIN_GNT] = 0x04;
    pdev->config[PCI_MAX_LAT] = 0x18;
    pci_set_word(pdev->wmask + PCI_COMMAND,
                 PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
                 PCI_COMMAND_PARITY);
    pdev->wmask[PCI_CACHE_LINE_SIZE] = 0;
    pdev->wmask[PCI_LATENCY_TIMER] = 0xf8;

    pm_cap = pci_pm_init(pdev, CS4281_PM_CAP, errp);
    if (pm_cap < 0) {
        return;
    }
    pci_set_word(pdev->config + pm_cap + PCI_PM_PMC, 0x7e21);
    pci_set_word(pdev->wmask + pm_cap + PCI_PM_CTRL,
                 PCI_PM_CTRL_STATE_MASK | PCI_PM_CTRL_PME_ENABLE);
    pci_set_word(pdev->w1cmask + pm_cap + PCI_PM_CTRL,
                 PCI_PM_CTRL_PME_STATUS);

    memset(pdev->wmask + CS4281_CFG_CWPR, 0, 0x20);
    pci_set_long(pdev->wmask + CS4281_CFG_CWPR, UINT32_MAX);
    pci_set_long(pdev->wmask + CS4281_CFG_GPIOR, UINT32_MAX);
    pci_set_long(pdev->wmask + CS4281_CFG_SPMC, UINT32_MAX);
    pci_set_long(pdev->wmask + CS4281_CFG_CFLR, UINT32_MAX);
    pci_set_long(pdev->wmask + CS4281_CFG_IISR, UINT32_MAX);
    pci_set_long(pdev->wmask + CS4281_CFG_SSVID, UINT32_MAX);
    memory_region_init_io(&s->ba0, OBJECT(s), &cs4281_ba0_ops, s,
                          "cs4281.ba0", CS4281_BA0_SIZE);
    memory_region_init_io(&s->ba1, OBJECT(s), &cs4281_ba1_ops, s,
                          "cs4281.ba1", CS4281_BA1_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->ba0);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->ba1);

    pdev->config_write = cs4281_config_write;
    s->streams_bh = qemu_bh_new_guarded(cs4281_update_streams_bh, s,
                                       &DEVICE(s)->mem_reentrancy_guard);
    cs4281_reset(DEVICE(s));
}

static void cs4281_exit(PCIDevice *pdev)
{
    CS4281State *s = CS4281(pdev);

    qemu_bh_cancel(s->streams_bh);
    qemu_bh_delete(s->streams_bh);
    s->streams_bh = NULL;
    audio_be_close_out(s->audio_be, s->voice_out);
    audio_be_close_in(s->audio_be, s->voice_in);
}

static const Property cs4281_properties[] = {
    DEFINE_AUDIO_PROPERTIES(CS4281State, audio_be),
};

static void cs4281_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    pc->realize = cs4281_realize;
    pc->exit = cs4281_exit;
    pc->vendor_id = 0x1013;
    pc->device_id = 0x6005;
    pc->revision = 0x01;
    pc->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    pc->subsystem_vendor_id = 0x8086;
    pc->subsystem_id = 0x4253;
    dc->desc = "Cirrus Logic Crystal CS4281 PCI Audio";
    dc->vmsd = &vmstate_cs4281;
    device_class_set_props(dc, cs4281_properties);
    device_class_set_legacy_reset(dc, cs4281_reset);
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
}

static const TypeInfo cs4281_info = {
    .name = TYPE_CS4281,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(CS4281State),
    .class_init = cs4281_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void cs4281_register_types(void)
{
    type_register_static(&cs4281_info);
    audio_register_model("cs4281", "Cirrus Logic CS4281", TYPE_CS4281);
}

type_init(cs4281_register_types)
