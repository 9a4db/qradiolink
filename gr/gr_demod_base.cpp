// Written by Adrian Musceac YO8RZZ , started March 2016.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include "gr_demod_base.h"

gr_demod_base::gr_demod_base(gr::qtgui::sink_c::sptr fft_gui,
                             gr::qtgui::const_sink_c::sptr const_gui, gr::qtgui::number_sink::sptr rssi_gui,
                              QObject *parent, float device_frequency,
                             float rf_gain, std::string device_args, std::string device_antenna,
                              int freq_corr) :
    QObject(parent)
{
    _msg_nr = 0;

    _device_frequency = device_frequency;
    _top_block = gr::make_top_block("demodulator");
    _mode = 9999;
    _carrier_offset = 25000;

    _signal_source = gr::analog::sig_source_c::make(1000000,gr::analog::GR_COS_WAVE,-_carrier_offset,1);
    _multiply = gr::blocks::multiply_cc::make();

    _audio_sink = make_gr_audio_sink();
    _vector_sink = make_gr_vector_sink();

    _message_sink = gr::blocks::message_debug::make();

    _rssi_valve = gr::blocks::copy::make(8);
    _rssi_valve->set_enabled(false);
    _fft_valve = gr::blocks::copy::make(8);
    _fft_valve->set_enabled(false);
    _const_valve = gr::blocks::copy::make(8);
    _const_valve->set_enabled(false);
    _mag_squared = gr::blocks::complex_to_mag_squared::make();
    _single_pole_filter = gr::filter::single_pole_iir_filter_ff::make(0.04);
    _log10 = gr::blocks::nlog10_ff::make();
    _multiply_const_ff = gr::blocks::multiply_const_ff::make(10);
    _agc2 = gr::analog::agc2_ff::make(0.6e-1, 1e-3, 1, 1);
    _moving_average = gr::blocks::moving_average_ff::make(25000,1,2000);
    _add_const = gr::blocks::add_const_ff::make(-110);


    _osmosdr_source = osmosdr::source::make(device_args);
    _osmosdr_source->set_center_freq(_device_frequency - _carrier_offset);
    _osmosdr_source->set_bandwidth(2000000);
    _osmosdr_source->set_sample_rate(1000000);
    _osmosdr_source->set_freq_corr(freq_corr);
    _osmosdr_source->set_gain_mode(false);
    _osmosdr_source->set_dc_offset_mode(0);
    _osmosdr_source->set_iq_balance_mode(0);
    _osmosdr_source->set_antenna(device_antenna);
    osmosdr::gain_range_t range = _osmosdr_source->get_gain_range();
    if (!range.empty())
    {
        double gain =  range.start() + rf_gain*(range.stop()-range.start());
        _osmosdr_source->set_gain(gain);
    }
    else
    {
        _osmosdr_source->set_gain_mode(true);
    }

    _constellation = const_gui;
    _fft_gui = fft_gui;
    _rssi = rssi_gui;

    _top_block->connect(_osmosdr_source,0,_multiply,0);
    _top_block->connect(_signal_source,0,_multiply,1);
    _top_block->connect(_multiply,0,_fft_valve,0);
    _top_block->connect(_fft_valve,0,_fft_gui,0);
    _top_block->msg_connect(_fft_gui,"freq",_message_sink,"store");


    _top_block->connect(_rssi_valve,0,_mag_squared,0);
    _top_block->connect(_mag_squared,0,_moving_average,0);
    _top_block->connect(_moving_average,0,_single_pole_filter,0);
    _top_block->connect(_single_pole_filter,0,_log10,0);
    _top_block->connect(_log10,0,_multiply_const_ff,0);
    _top_block->connect(_multiply_const_ff,0,_add_const,0);
    _top_block->connect(_add_const,0,_rssi,0);




    _2fsk = make_gr_demod_2fsk_sdr(125,1000000,1700,4000);
    _4fsk_2k = make_gr_demod_4fsk_sdr(250,1000000,1700,2000);
    _4fsk_10k = make_gr_demod_4fsk_sdr(50,1000000,1700,10000);
    _am = make_gr_demod_am_sdr(0, 1000000,1700,4000);
    _bpsk_1k = make_gr_demod_bpsk_sdr(250,1000000,1700,1300,2);
    _bpsk_2k = make_gr_demod_bpsk_sdr(125,1000000,1700,2500,1);
    _fm_2500 = make_gr_demod_nbfm_sdr(0, 1000000,1700,2500);
    _fm_5000 = make_gr_demod_nbfm_sdr(0, 1000000,1700,4000);
    _qpsk_2k = make_gr_demod_qpsk_sdr(250,1000000,1700,800);
    _qpsk_10k = make_gr_demod_qpsk_sdr(50,1000000,1700,4000);
    _qpsk_250k = make_gr_demod_qpsk_sdr(2,1000000,1700,65000);
    _qpsk_video = make_gr_demod_qpsk_sdr(2,1000000,1700,65000);
    _ssb = make_gr_demod_ssb_sdr(0, 1000000,1700,2500);
    _wfm = make_gr_demod_wbfm_sdr(0, 1000000,1700,75000);

}

gr_demod_base::~gr_demod_base()
{
    _osmosdr_source.reset();
}

void gr_demod_base::set_mode(int mode)
{
    _top_block->lock();

    switch(_mode)
    {
    case gr_modem_types::ModemType2FSK2000:
        _top_block->disconnect(_multiply,0,_2fsk,0);
        _top_block->disconnect(_2fsk,0,_rssi_valve,0);
        _top_block->disconnect(_2fsk,1,_const_valve,0);
        _top_block->disconnect(_const_valve,0,_constellation,0);
        break;
    case gr_modem_types::ModemType4FSK2000:
        _top_block->disconnect(_multiply,0,_4fsk_2k,0);
        _top_block->disconnect(_4fsk_2k,0,_rssi_valve,0);
        _top_block->disconnect(_4fsk_2k,1,_const_valve,0);
        _top_block->disconnect(_const_valve,0,_constellation,0);
        _top_block->disconnect(_4fsk_2k,2,_vector_sink,0);
        break;
    case gr_modem_types::ModemType4FSK20000:
        _top_block->disconnect(_multiply,0,_4fsk_10k,0);
        _top_block->disconnect(_4fsk_10k,0,_rssi_valve,0);
        _top_block->disconnect(_4fsk_10k,1,_const_valve,0);
        _top_block->disconnect(_const_valve,0,_constellation,0);
        _top_block->disconnect(_4fsk_10k,2,_vector_sink,0);
        break;
    case gr_modem_types::ModemTypeAM5000:
        _top_block->disconnect(_multiply,0,_am,0);
        _top_block->disconnect(_am,0,_rssi_valve,0);
        _top_block->disconnect(_am,1,_audio_sink,0);
        break;
    case gr_modem_types::ModemTypeBPSK1000:
        _top_block->disconnect(_multiply,0,_bpsk_1k,0);
        _top_block->disconnect(_bpsk_1k,0,_rssi_valve,0);
        _top_block->disconnect(_bpsk_1k,1,_const_valve,0);
        _top_block->disconnect(_const_valve,0,_constellation,0);
        break;
    case gr_modem_types::ModemTypeBPSK2000:
        _top_block->disconnect(_multiply,0,_bpsk_2k,0);
        _top_block->disconnect(_bpsk_2k,0,_rssi_valve,0);
        _top_block->disconnect(_bpsk_2k,1,_const_valve,0);
        _top_block->disconnect(_const_valve,0,_constellation,0);
        break;
    case gr_modem_types::ModemTypeNBFM2500:
        _top_block->disconnect(_multiply,0,_fm_2500,0);
        _top_block->disconnect(_fm_2500,0,_rssi_valve,0);
        _top_block->disconnect(_fm_2500,1,_audio_sink,0);
        break;
    case gr_modem_types::ModemTypeNBFM5000:
        _top_block->disconnect(_multiply,0,_fm_5000,0);
        _top_block->disconnect(_fm_5000,0,_rssi_valve,0);
        _top_block->disconnect(_fm_5000,1,_audio_sink,0);
        break;
    case gr_modem_types::ModemTypeQPSK2000:
        _top_block->disconnect(_multiply,0,_qpsk_2k,0);
        _top_block->disconnect(_qpsk_2k,0,_rssi_valve,0);
        _top_block->disconnect(_qpsk_2k,1,_const_valve,0);
        _top_block->disconnect(_const_valve,0,_constellation,0);
        _top_block->disconnect(_qpsk_2k,2,_vector_sink,0);
        break;
    case gr_modem_types::ModemTypeQPSK20000:
        _top_block->disconnect(_multiply,0,_qpsk_10k,0);
        _top_block->disconnect(_qpsk_10k,0,_rssi_valve,0);
        _top_block->disconnect(_qpsk_10k,1,_const_valve,0);
        _top_block->disconnect(_const_valve,0,_constellation,0);
        _top_block->disconnect(_qpsk_10k,2,_vector_sink,0);
        break;
    case gr_modem_types::ModemTypeQPSK250000:
        _top_block->disconnect(_multiply,0,_qpsk_250k,0);
        _top_block->disconnect(_qpsk_250k,0,_rssi_valve,0);
        _top_block->disconnect(_qpsk_250k,1,_const_valve,0);
        _top_block->disconnect(_const_valve,0,_constellation,0);
        _top_block->disconnect(_qpsk_250k,2,_vector_sink,0);
        _carrier_offset = 25000;
        _signal_source->set_frequency(-_carrier_offset);
        _osmosdr_source->set_center_freq(_device_frequency - _carrier_offset);
        break;
    case gr_modem_types::ModemTypeQPSKVideo:
        _top_block->disconnect(_multiply,0,_qpsk_video,0);
        _top_block->disconnect(_qpsk_video,0,_rssi_valve,0);
        _top_block->disconnect(_qpsk_video,1,_const_valve,0);
        _top_block->disconnect(_const_valve,0,_constellation,0);
        _top_block->disconnect(_qpsk_video,2,_vector_sink,0);
        _carrier_offset = 25000;
        _signal_source->set_frequency(-_carrier_offset);
        _osmosdr_source->set_center_freq(_device_frequency - _carrier_offset);
        break;
    case gr_modem_types::ModemTypeSSB2500:
        _top_block->disconnect(_multiply,0,_ssb,0);
        _top_block->disconnect(_ssb,0,_rssi_valve,0);
        _top_block->disconnect(_ssb,1,_audio_sink,0);
        break;
    case gr_modem_types::ModemTypeWBFM:
        _top_block->disconnect(_multiply,0,_wfm,0);
        _top_block->disconnect(_wfm,0,_rssi_valve,0);
        _top_block->disconnect(_wfm,1,_audio_sink,0);
        _carrier_offset = 25000;
        _signal_source->set_frequency(-_carrier_offset);
        _osmosdr_source->set_center_freq(_device_frequency - _carrier_offset);
        break;
    default:
        break;
    }

    switch(mode)
    {
    case gr_modem_types::ModemType2FSK2000:
        _signal_source->set_sampling_freq(1000000);
        _osmosdr_source->set_sample_rate(1000000);
        _add_const->set_k(-110);
        _top_block->connect(_multiply,0,_2fsk,0);
        _top_block->connect(_2fsk,0,_rssi_valve,0);
        _top_block->connect(_2fsk,1,_const_valve,0);
        _top_block->connect(_const_valve,0,_constellation,0);
        break;
    case gr_modem_types::ModemType4FSK2000:
        _signal_source->set_sampling_freq(1000000);
        _osmosdr_source->set_sample_rate(1000000);
        _add_const->set_k(-110);
        _top_block->connect(_multiply,0,_4fsk_2k,0);
        _top_block->connect(_4fsk_2k,0,_rssi_valve,0);
        _top_block->connect(_4fsk_2k,1,_const_valve,0);
        _top_block->connect(_const_valve,0,_constellation,0);
        _top_block->connect(_4fsk_2k,2,_vector_sink,0);
        break;
    case gr_modem_types::ModemType4FSK20000:
        _signal_source->set_sampling_freq(1000000);
        _osmosdr_source->set_sample_rate(1000000);
        _add_const->set_k(-110);
        _top_block->connect(_multiply,0,_4fsk_10k,0);
        _top_block->connect(_4fsk_10k,0,_rssi_valve,0);
        _top_block->connect(_4fsk_10k,1,_const_valve,0);
        _top_block->connect(_const_valve,0,_constellation,0);
        _top_block->connect(_4fsk_10k,2,_vector_sink,0);
        break;
    case gr_modem_types::ModemTypeAM5000:
        _signal_source->set_sampling_freq(1000000);
        _osmosdr_source->set_sample_rate(1000000);
        _add_const->set_k(-55);
        _top_block->connect(_multiply,0,_am,0);
        _top_block->connect(_am,0,_rssi_valve,0);
        _top_block->connect(_am,1,_audio_sink,0);
        break;
    case gr_modem_types::ModemTypeBPSK1000:
        _signal_source->set_sampling_freq(1000000);
        _osmosdr_source->set_sample_rate(1000000);
        _add_const->set_k(-110);
        _top_block->connect(_multiply,0,_bpsk_1k,0);
        _top_block->connect(_bpsk_1k,0,_rssi_valve,0);
        _top_block->connect(_bpsk_1k,1,_const_valve,0);
        _top_block->connect(_const_valve,0,_constellation,0);
        break;
    case gr_modem_types::ModemTypeBPSK2000:
        _signal_source->set_sampling_freq(1000000);
        _osmosdr_source->set_sample_rate(1000000);
        _add_const->set_k(-110);
        _top_block->connect(_multiply,0,_bpsk_2k,0);
        _top_block->connect(_bpsk_2k,0,_rssi_valve,0);
        _top_block->connect(_bpsk_2k,1,_const_valve,0);
        _top_block->connect(_const_valve,0,_constellation,0);
        break;
    case gr_modem_types::ModemTypeNBFM2500:
        _signal_source->set_sampling_freq(1000000);
        _osmosdr_source->set_sample_rate(1000000);
        _add_const->set_k(-55);
        _top_block->connect(_multiply,0,_fm_2500,0);
        _top_block->connect(_fm_2500,0,_rssi_valve,0);
        _top_block->connect(_fm_2500,1,_audio_sink,0);
        break;
    case gr_modem_types::ModemTypeNBFM5000:
        _signal_source->set_sampling_freq(1000000);
        _osmosdr_source->set_sample_rate(1000000);
        _add_const->set_k(-55);
        _top_block->connect(_multiply,0,_fm_5000,0);
        _top_block->connect(_fm_5000,0,_rssi_valve,0);
        _top_block->connect(_fm_5000,1,_audio_sink,0);
        break;
    case gr_modem_types::ModemTypeQPSK2000:
        _signal_source->set_sampling_freq(1000000);
        _osmosdr_source->set_sample_rate(1000000);
        _add_const->set_k(-110);
        _top_block->connect(_multiply,0,_qpsk_2k,0);
        _top_block->connect(_qpsk_2k,0,_rssi_valve,0);
        _top_block->connect(_qpsk_2k,1,_const_valve,0);
        _top_block->connect(_const_valve,0,_constellation,0);
        _top_block->connect(_qpsk_2k,2,_vector_sink,0);
        break;
    case gr_modem_types::ModemTypeQPSK20000:
        _signal_source->set_sampling_freq(1000000);
        _osmosdr_source->set_sample_rate(1000000);
        _add_const->set_k(-110);
        _top_block->connect(_multiply,0,_qpsk_10k,0);
        _top_block->connect(_qpsk_10k,0,_rssi_valve,0);
        _top_block->connect(_qpsk_10k,1,_const_valve,0);
        _top_block->connect(_const_valve,0,_constellation,0);
        _top_block->connect(_qpsk_10k,2,_vector_sink,0);
        break;
    case gr_modem_types::ModemTypeQPSK250000:
        _signal_source->set_sampling_freq(1000000);
        _osmosdr_source->set_sample_rate(1000000);
        _carrier_offset = 250000;
        _signal_source->set_frequency(-_carrier_offset);
        _osmosdr_source->set_center_freq(_device_frequency - _carrier_offset);
        _add_const->set_k(-110);
        _top_block->connect(_multiply,0,_qpsk_250k,0);
        _top_block->connect(_qpsk_250k,0,_rssi_valve,0);
        _top_block->connect(_qpsk_250k,1,_const_valve,0);
        _top_block->connect(_const_valve,0,_constellation,0);
        _top_block->connect(_qpsk_250k,2,_vector_sink,0);
        break;
    case gr_modem_types::ModemTypeQPSKVideo:
        _signal_source->set_sampling_freq(1000000);
        _osmosdr_source->set_sample_rate(1000000);
        _carrier_offset = 250000;
        _signal_source->set_frequency(-_carrier_offset);
        _osmosdr_source->set_center_freq(_device_frequency - _carrier_offset);
        _add_const->set_k(-110);
        _top_block->connect(_multiply,0,_qpsk_video,0);
        _top_block->connect(_qpsk_video,0,_rssi_valve,0);
        _top_block->connect(_qpsk_video,1,_const_valve,0);
        _top_block->connect(_const_valve,0,_constellation,0);
        _top_block->connect(_qpsk_video,2,_vector_sink,0);
        break;
    case gr_modem_types::ModemTypeSSB2500:
        _signal_source->set_sampling_freq(1000000);
        _osmosdr_source->set_sample_rate(1000000);
        _add_const->set_k(-55);
        _top_block->connect(_multiply,0,_ssb,0);
        _top_block->connect(_ssb,0,_rssi_valve,0);
        _top_block->connect(_ssb,1,_audio_sink,0);
        break;
    case gr_modem_types::ModemTypeWBFM:
        _signal_source->set_sampling_freq(1000000);
        _osmosdr_source->set_sample_rate(1000000);
        _carrier_offset = 250000;
        _signal_source->set_frequency(-_carrier_offset);
        _osmosdr_source->set_center_freq(_device_frequency - _carrier_offset);
        _add_const->set_k(-55);
        _top_block->connect(_multiply,0,_wfm,0);
        _top_block->connect(_wfm,0,_rssi_valve,0);
        _top_block->connect(_wfm,1,_audio_sink,0);
    default:
        break;
    }
    _mode = mode;

    _top_block->unlock();
}

void gr_demod_base::start()
{
    _top_block->start();
}

void gr_demod_base::stop()
{
    _top_block->stop();
    _top_block->wait();
}

std::vector<unsigned char>* gr_demod_base::getFrame1()
{
    std::vector<unsigned char> *data;
    switch(_mode)
    {
    case gr_modem_types::ModemType2FSK2000:
        data = _2fsk->getFrame1();
        break;
    case gr_modem_types::ModemTypeBPSK1000:
        data = _bpsk_1k->getFrame1();
        break;
    case gr_modem_types::ModemTypeBPSK2000:
        data = _bpsk_2k->getFrame1();
        break;
    }
    return data;
}

std::vector<unsigned char>* gr_demod_base::getFrame2()
{
    std::vector<unsigned char> *data;
    switch(_mode)
    {
    case gr_modem_types::ModemType2FSK2000:
        data = _2fsk->getFrame2();
        break;
    case gr_modem_types::ModemTypeBPSK1000:
        data = _bpsk_1k->getFrame2();
        break;
    case gr_modem_types::ModemTypeBPSK2000:
        data = _bpsk_2k->getFrame2();
        break;
    }
    return data;
}


std::vector<unsigned char>* gr_demod_base::getData()
{
    std::vector<unsigned char> *data = _vector_sink->get_data();
    return data;
}

std::vector<float>* gr_demod_base::getAudio()
{
    std::vector<float> *data = _audio_sink->get_data();
    return data;
}

void gr_demod_base::tune(long center_freq)
{
    _device_frequency = center_freq;
    _osmosdr_source->set_center_freq(_device_frequency-_carrier_offset);
}

double gr_demod_base::get_freq()
{
    int n = _message_sink->num_messages();
    if(n > _msg_nr)
    {
        _msg_nr = n;
        pmt::pmt_t msg = _message_sink->get_message(n - 1);
        return pmt::to_double(pmt::cdr(msg));
    }
    else
    {
        return 0;
    }
}

void gr_demod_base::set_rx_sensitivity(float value)
{
    osmosdr::gain_range_t range = _osmosdr_source->get_gain_range();
    if (!range.empty())
    {
        double gain =  range.start() + value*(range.stop()-range.start());
        _osmosdr_source->set_gain(gain);
    }
}

void gr_demod_base::enable_gui_const(bool value)
{
    _rssi_valve->set_enabled(value);
    _const_valve->set_enabled(value);
}

void gr_demod_base::enable_gui_fft(bool value)
{
    _fft_valve->set_enabled(value);
}

void gr_demod_base::set_squelch(int value)
{
    _fm_2500->set_squelch(value);
    _fm_5000->set_squelch(value);
    _am->set_squelch(value);
    _ssb->set_squelch(value);
    _wfm->set_squelch(value);
}

void gr_demod_base::set_ctcss(float value)
{
    _top_block->lock();
    _fm_2500->set_ctcss(value);
    _fm_5000->set_ctcss(value);
    _top_block->unlock();
}
