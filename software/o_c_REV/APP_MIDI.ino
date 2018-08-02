// See https://www.pjrc.com/teensy/td_midi.html

#include "braids_quantizer.h"
#include "braids_quantizer_scales.h"
#include "OC_scales.h"

const uint8_t MIDI_MSG_NOTE_ON = 1;
const uint8_t MIDI_MSG_NOTE_OFF = 0;
const uint8_t MIDI_MSG_MIDI_CC = 3;
const uint8_t MIDI_MSG_AFTERTOUCH = 5;
const uint8_t MIDI_MSG_PITCHBEND = 6;
const uint8_t MIDI_MSG_SYSEX = 7;
const uint16_t MIDI_INDICATOR_COUNTDOWN = 2000;

enum MIDI_IN_FUNCTION {
    MIDI_IN_NOTE,
    MIDI_IN_GATE,
    MIDI_IN_TRIGGER,
    MIDI_IN_VELOCITY,
    MIDI_IN_MOD,
    MIDI_IN_AFTERTOUCH,
    MIDI_IN_PITCHBEND
};

enum MIDI_OUT_FUNCTION {
    MIDI_OUT_NOTE,
    MIDI_OUT_LEGATO,
    MIDI_OUT_VELOCITY,
    MIDI_OUT_MOD,
    MIDI_OUT_AFTERTOUCH,
    MIDI_OUT_PITCHBEND,
};

enum MIDI_SETTINGS {
    MIDI_SETTING_1_OUT_1_FN,
    MIDI_SETTING_1_OUT_1_CHANNEL,
    MIDI_SETTING_1_OUT_2_FN,
    MIDI_SETTING_1_OUT_2_CHANNEL,
    MIDI_SETTING_1_OUT_3_FN,
    MIDI_SETTING_1_OUT_3_CHANNEL,
    MIDI_SETTING_1_OUT_4_FN,
    MIDI_SETTING_1_OUT_4_CHANNEL,

    MIDI_SETTING_1_IN_A_FN,
    MIDI_SETTING_1_IN_A_CHANNEL,
    MIDI_SETTING_1_IN_B_FN,
    MIDI_SETTING_1_IN_B_CHANNEL,
    MIDI_SETTING_1_IN_C_FN,
    MIDI_SETTING_1_IN_C_CHANNEL,
    MIDI_SETTING_1_IN_D_FN,
    MIDI_SETTING_1_IN_D_CHANNEL,

    MIDI_SETTING_2_OUT_1_FN,
    MIDI_SETTING_2_OUT_1_CHANNEL,
    MIDI_SETTING_2_OUT_2_FN,
    MIDI_SETTING_2_OUT_2_CHANNEL,
    MIDI_SETTING_2_OUT_3_FN,
    MIDI_SETTING_2_OUT_3_CHANNEL,
    MIDI_SETTING_2_OUT_4_FN,
    MIDI_SETTING_2_OUT_4_CHANNEL,

    MIDI_SETTING_2_IN_A_FN,
    MIDI_SETTING_2_IN_A_CHANNEL,
    MIDI_SETTING_2_IN_B_FN,
    MIDI_SETTING_2_IN_B_CHANNEL,
    MIDI_SETTING_2_IN_C_FN,
    MIDI_SETTING_2_IN_C_CHANNEL,
    MIDI_SETTING_2_IN_D_FN,
    MIDI_SETTING_2_IN_D_CHANNEL,

    MIDI_SETTING_1_TRANSPOSE_OUT_1,
    MIDI_SETTING_1_TRANSPOSE_OUT_2,
    MIDI_SETTING_1_TRANSPOSE_OUT_3,
    MIDI_SETTING_1_TRANSPOSE_OUT_4,
    MIDI_SETTING_1_TRANSPOSE_IN_1,
    MIDI_SETTING_1_TRANSPOSE_IN_2,
    MIDI_SETTING_1_TRANSPOSE_IN_3,
    MIDI_SETTING_1_TRANSPOSE_IN_4,

    MIDI_SETTING_2_TRANSPOSE_OUT_1,
    MIDI_SETTING_2_TRANSPOSE_OUT_2,
    MIDI_SETTING_2_TRANSPOSE_OUT_3,
    MIDI_SETTING_2_TRANSPOSE_OUT_4,
    MIDI_SETTING_2_TRANSPOSE_IN_1,
    MIDI_SETTING_2_TRANSPOSE_IN_2,
    MIDI_SETTING_2_TRANSPOSE_IN_3,
    MIDI_SETTING_2_TRANSPOSE_IN_4,

    MIDI_SETTING_SELECTED_SETUP,

    MIDI_SETTING_LAST
};

class MIDIInterface : public settings::SettingsBase<MIDIInterface, MIDI_SETTING_LAST> {
public:
    menu::ScreenCursor<menu::kScreenLines> cursor;

    void Start() {
        screen = 1;
        cursor.Init(MIDI_SETTING_1_OUT_1_FN, MIDI_SETTING_1_IN_D_CHANNEL);
        quantizer.Init();
        quantizer.Configure(OC::Scales::GetScale(5), 0xffff); // Semi-tone

        for (int ch = 0; ch < 4; ch++)
        {
            note_in[ch] = -1;
            note_out[ch] = -1;
            indicator_in[ch] = 0;
            indicator_out[ch] = 0;
            Out(ch, 0);
        }
	}

    void Resume() {
        if (values_[MIDI_SETTING_SELECTED_SETUP] == 0)
            cursor.Init(MIDI_SETTING_1_OUT_1_FN, MIDI_SETTING_1_IN_D_CHANNEL);
        else
            cursor.Init(MIDI_SETTING_2_OUT_1_FN, MIDI_SETTING_2_IN_D_CHANNEL);
    }

    void Controller() {
        midi_in();
        midi_out();

        // Handle clock timing
        for (int ch = 0; ch < 4; ch++)
        {
            if (clock_countdown[ch] > 0) {
                if (--clock_countdown[ch] == 0) Out(ch, 0);
            }
            if (indicator_in[ch] > 0) --indicator_in[ch];
            if (indicator_out[ch] > 0) --indicator_out[ch];
        }
    }

    void View() {
        const uint8_t midi_icon[8] = {0x3c, 0x42, 0x91, 0x45, 0x45, 0x91, 0x42, 0x3c};
        const uint8_t note_icon[8] = {0xc0, 0xe0, 0xe0, 0xe0, 0x7f, 0x02, 0x14, 0x08};

        menu::DefaultTitleBar::Draw();
        graphics.print("MIDI Setup");
        graphics.print(get_setup_number() + 1);
        graphics.print(screen ? " Assign" : " Transpose");

        menu::SettingsList<menu::kScreenLines, 0, menu::kDefaultValueX - 1> settings_list(cursor);
        menu::SettingsListItem list_item;
        while (settings_list.available())
        {
            const int current = settings_list.Next(list_item);
            const int value = get_value(current);

            // Note indicator for transpose (if the channel is assigned to a Note type)
            if (screen == 0) {
                // This will be a number from 0-7, with 0-3 being Out positions
                int tr_menu_position = current - 32 - (8 * get_setup_number());
                if (tr_menu_position < 4) {
                    int ix = get_out_index(tr_menu_position);
                    if (values_[ix + 1] > 0 && (values_[ix] == MIDI_OUT_NOTE || values_[ix] == MIDI_OUT_LEGATO)) {
                        graphics.drawBitmap8(64, list_item.y + 2, 8, note_icon);
                    }
                } else {
                    int ix = get_in_index(tr_menu_position - 4);
                    if (values_[ix + 1] > 0 && values_[ix] == MIDI_IN_NOTE) {
                        graphics.drawBitmap8(64, list_item.y + 2, 8, note_icon);
                    }
                }
            }

            // MIDI In and Out indicators
            if (screen == 1) {
                int sn = current - (16 * get_setup_number());
                if (sn > 7 && sn < 16 && sn % 2 == 0) { // It's a MIDI In assignment
                    int ch = (sn - 8) / 2;
                    if (indicator_in[ch] > 0 || note_in[ch] > -1) {
                        graphics.drawBitmap8(64, list_item.y + 2, 8, midi_icon);
                    }
                }
                if (sn < 8 && sn % 2 == 0) { // It's a MIDI Out assignment
                    int ch = sn / 2;
                    if (indicator_out[ch] > 0 || note_out[ch] > -1) {
                        graphics.drawBitmap8(64, list_item.y + 2, 8, midi_icon);
                    }
                }
            }

            list_item.DrawDefault(value, MIDIInterface::value_attr(current));
        }
    }

    void SelectSetup(int setup_number, int new_screen = -1) {
        // Stay the same if not provided
        if (new_screen == -1) new_screen = screen;

        // Save current cursor position
        cursor_pos[get_setup_number()][screen] = cursor.cursor_pos();

        int start;
        int end;
        if (setup_number == 0) {
            // Setup 1
            if (new_screen == 0) {
                start = MIDI_SETTING_1_TRANSPOSE_OUT_1;
                end = MIDI_SETTING_1_TRANSPOSE_IN_4;
            } else {
                start = MIDI_SETTING_1_OUT_1_FN;
                end = MIDI_SETTING_1_IN_D_CHANNEL;
            }
        } else {
            // Setup 2
            if (new_screen == 0) {
                start = MIDI_SETTING_2_TRANSPOSE_OUT_1;
                end = MIDI_SETTING_2_TRANSPOSE_IN_4;
            } else {
                start = MIDI_SETTING_2_OUT_1_FN;
                end = MIDI_SETTING_2_IN_D_CHANNEL;
            }
        }

        int last_cursor_position = cursor_pos[setup_number][new_screen];
        if (last_cursor_position == 0) last_cursor_position = start;
        cursor.Init(start, end);
        cursor.Scroll(last_cursor_position - start);
        values_[MIDI_SETTING_SELECTED_SETUP] = setup_number;
        screen = new_screen;
    }

    void SwitchScreen() {
        SelectSetup(get_setup_number(), 1 - screen);
    }

    void Panic() {
        // Reset the interface states
        for (int ch = 0; ch < 4; ch++)
        {
            note_in[ch] = -1;
            note_out[ch] = -1;
            indicator_in[ch] = 0;
            indicator_out[ch] = 0;
        }

        // Send all notes off on every channel
        for (int note = 0; note < 128; note++)
        {
            for (int channel = 1; channel <= 16; channel++)
            {
                usbMIDI.sendNoteOff(note, 0, channel);
            }
            usbMIDI.send_now();
        }
    }

private:
    // Quantizer for note numbers
    braids::Quantizer quantizer;

    // Housekeeping
    uint8_t cursor_pos[2][2]; // For cursor position of [Setup][Screen]
    int clock_countdown[4]; // For clock output timing
    bool screen; // 0=Transpose 1=Assign

    // MIDI In
    int note_in[4]; // Up to four notes at a time are kept track of with MIDI In
    uint16_t indicator_in[4]; // A MIDI indicator will display next to MIDI In assignment

    // MIDI Out
    int adc_lag_countdown[4]; // Lag countdown for each input channel
    bool gated[4]; // Current gated status of each input
    int note_out[4]; // Most recent note from this input channel
    int last_channel[4]; // Keep track of the actual send channel, in case it's changed while the note is on
    int legato_on[4]; // The note handler may currently respond to legato note changes
    int last_cv[4]; // To determine whether a new CV value needs to be handled for MIDI controllers
    uint16_t indicator_out[4]; // A MIDI indicator will display next to MIDI Out assignment

    int get_setup_number() {
        return values_[MIDI_SETTING_SELECTED_SETUP];
    }

    void midi_out() {
        for (int ch = 0; ch < 4; ch++)
        {
            int ix = get_out_index(ch);
            int out_fn = values_[ix];
            int out_ch = values_[ix + 1];
            if (out_ch == 0) continue;
            bool indicator = 0;

            if (out_fn == MIDI_OUT_NOTE || out_fn == MIDI_OUT_LEGATO) {
                bool read_gate = Gate(ch);
                bool legato = out_fn == MIDI_OUT_LEGATO;

                // Prepare to read pitch and send gate in the near future; there's a slight
                // lag between when a gate is read and when the CV can be read.
                if (read_gate && !gated[ch]) StartADCLag(ch);
                bool note_on = EndOfADCLag(ch); // If the ADC lag has ended, a note will always be sent

                if (note_on || legato_on[ch]) {
                    // Get a new reading when gated, or when checking for legato changes
                    quantizer.Process(In(ch), 0, 0);
                    uint8_t midi_note = quantizer.NoteNumber() + get_out_transpose(ch);
                    midi_note = constrain(midi_note, 0, 127);

                    if (legato_on[ch] && midi_note != note_out[ch]) {
                        // Send note off if the note has changed
                        usbMIDI.sendNoteOff(note_out[ch], 0, last_channel[ch]);
                        note_out[ch] = -1;
                        indicator = 1;
                        note_on = 1;
                    }

                    if (note_on) {
                        int velocity = 0x64;
                        // Look for an input assigned to velocity on the same channel and, if found, use it
                        for (int vch = 0; vch < 4; vch++)
                        {
                            int i = get_out_index(vch);
                            if (values_[i] == MIDI_OUT_VELOCITY && values_[i + 1] == out_ch) {
                                velocity = Proportion(In(vch), HEMISPHERE_MAX_CV, 127);
                            }
                        }
                        usbMIDI.sendNoteOn(midi_note, velocity, out_ch);
                        indicator = 1;
                        note_out[ch] = midi_note;
                        last_channel[ch] = out_ch;
                        if (legato) legato_on[ch] = 1;
                    }
                }

                if (!read_gate && gated[ch]) { // A note off message should be sent
                    usbMIDI.sendNoteOff(note_out[ch], 0, last_channel[ch]);
                    note_out[ch] = -1;
                    indicator = 1;
                }

                gated[ch] = read_gate;
                if (!gated[ch]) legato_on[ch] = 0;
            }

            // Handle other messages
            int this_cv = In(ch);
            if (cv_has_changed(this_cv, last_cv[ch])) {
                last_cv[ch] = this_cv;

                // Modulation wheel
                if (out_fn == MIDI_OUT_MOD) {
                    int value = Proportion(this_cv, HEMISPHERE_MAX_CV, 127);
                    usbMIDI.sendControlChange(1, value, out_ch);
                    indicator = 1;
                }

                // Aftertouch
                if (out_fn == MIDI_OUT_AFTERTOUCH) {
                    int value = Proportion(this_cv, HEMISPHERE_MAX_CV, 127);
                    usbMIDI.sendAfterTouch(value, out_ch);
                    indicator = 1;
                }

                // Pitch Bend
                if (out_fn == MIDI_OUT_PITCHBEND) {
                    uint16_t bend = Proportion(this_cv + (HEMISPHERE_MAX_CV / 2), HEMISPHERE_MAX_CV, 16383);
                    bend = constrain(bend, 0, 16383);
                    usbMIDI.sendPitchBend(bend, out_ch);
                    indicator = 1;
                }
            }

            if (indicator) indicator_out[ch] = MIDI_INDICATOR_COUNTDOWN;
        }
        usbMIDI.send_now();
    }

    void midi_in() {
        if (usbMIDI.read()) {
            int message = usbMIDI.getType();
            int channel = usbMIDI.getChannel();
            int data1 = usbMIDI.getData1();
            int data2 = usbMIDI.getData2();

            // A MIDI message has been received; go through each channel to see if it
            // needs to be routed to any of the CV outputs
            for (int ch = 0; ch < 4; ch++)
            {
                int ix = get_in_index(ch);
                int in_fn = values_[ix];
                int in_ch = values_[ix + 1];
                bool indicator = 0;
                if (message == MIDI_MSG_NOTE_ON && in_ch == channel) {
                    if (note_in[ch] == -1) { // If this channel isn't already occupied with another note, handle Note On
                        note_in[ch] = data1;
                        if (in_fn == MIDI_IN_NOTE) {
                            // Send quantized pitch CV. Isolate transposition to quantizer so that it notes off aren't
                            // misinterpreted if transposition is changed during the note.
                            int note = data1 + get_in_transpose(ch);
                            note = constrain(note, 0, 127);
                            Out(ch, quantizer.Lookup(note));
                            indicator = 1;
                        }

                        if (in_fn == MIDI_IN_GATE) {
                            // Send a gate at Note On
                            GateOut(ch, 1);
                            indicator = 1;
                        }

                        if (in_fn == MIDI_IN_TRIGGER) {
                            // Send a trigger pulse to CV
                            ClockOut(ch);
                            indicator = 1;
                        }

                        if (in_fn == MIDI_IN_VELOCITY) {
                            // Send velocity data to CV
                            Out(ch, Proportion(data2, 127, HEMISPHERE_MAX_CV));
                            indicator = 1;
                        }
                    }
                }

                if (message == MIDI_MSG_NOTE_OFF && in_ch == channel) {
                    if (note_in[ch] == data1) { // If the note off matches the note on assingned to this output
                        note_in[ch] = -1;
                        if (in_fn == MIDI_IN_GATE) {
                            // Turn off gate on Note Off
                            GateOut(ch, 0);
                            indicator = 1;
                        }
                    }
                }

                if (message == MIDI_MSG_MIDI_CC && in_fn == MIDI_IN_MOD && in_ch == channel) {
                    // Send mod wheel to CV
                    if (data1 == 1) {
                        int data = data2 << 8;
                        Out(ch, Proportion(data, 0x7fff, HEMISPHERE_MAX_CV));
                        indicator = 1;
                    }
                }

                if (message == MIDI_MSG_AFTERTOUCH && in_fn == MIDI_IN_AFTERTOUCH && in_ch == channel) {
                    // Send aftertouch to CV
                    int data = data2 << 8;
                    Out(ch, Proportion(data, 0x7fff, HEMISPHERE_MAX_CV));
                    indicator = 1;
                }

                if (message == MIDI_MSG_PITCHBEND && in_fn == MIDI_IN_PITCHBEND && in_ch == channel) {
                    // Send pitch bend to CV
                    int data = (data2 << 8) + data1 - 16384;
                    Out(ch, Proportion(data, 0x7fff, HEMISPHERE_MAX_CV));
                    indicator = 1;
                }

                if (indicator) indicator_in[ch] = MIDI_INDICATOR_COUNTDOWN;
            }
        }
    }

    int get_in_index(int ch) {
        int setup_offset = get_setup_number() * 16;
        return (ch * 2) + 8 + setup_offset;
    }

    int get_in_transpose(int ch) {
        int setup_offset = get_setup_number() * 8;
        return values_[36 + ch + setup_offset];
    }

    int get_out_index(int ch) {
        int setup_offset = values_[MIDI_SETTING_SELECTED_SETUP] * 16;
        return (ch * 2) + setup_offset;
    }

    int get_out_transpose(int ch) {
        int setup_offset = get_setup_number() * 8;
        return values_[32 + ch + setup_offset];
    }

    bool cv_has_changed(int this_cv, int last_cv) {
        int diff = this_cv - last_cv;
        return (diff > 50 || diff < -50) ? 1 : 0;
    }

    void Out(int ch, int value, int octave = 0) {
        OC::DAC::set_pitch((DAC_CHANNEL)ch, value, octave);
    }

    int In(int ch) {
        return OC::ADC::raw_pitch_value((ADC_CHANNEL)ch);
    }

    bool Gate(int ch) {
        bool high = 0;
        if (ch == 0) high = OC::DigitalInputs::read_immediate<OC::DIGITAL_INPUT_1>();
        if (ch == 1) high = OC::DigitalInputs::read_immediate<OC::DIGITAL_INPUT_2>();
        if (ch == 2) high = OC::DigitalInputs::read_immediate<OC::DIGITAL_INPUT_3>();
        if (ch == 3) high = OC::DigitalInputs::read_immediate<OC::DIGITAL_INPUT_4>();
        return high;
    }

    void GateOut(int ch, bool high) {
        Out(ch, 0, (high ? 5 : 0));
    }

    void ClockOut(int ch, int ticks = HEMISPHERE_CLOCK_TICKS) {
        clock_countdown[ch] = ticks;
        Out(ch, 0, 5);
    }

    void StartADCLag(int ch) {
        adc_lag_countdown[ch] = HEMISPHERE_ADC_LAG;
    }

    bool EndOfADCLag(int ch) {
        return (--adc_lag_countdown[ch] == 0);
    }

    int Proportion(int numerator, int denominator, int max_value) {
        simfloat proportion = int2simfloat((int32_t)numerator) / (int32_t)denominator;
        int scaled = simfloat2int(proportion * max_value);
        return scaled;
    }
};

const char* const midi_out_functions[6] = {
  "Note", "Leg.", "Veloc", "Mod", "Aft", "Bend"
};
const char* const midi_in_functions[7] = {
  "Note", "Gate", "Trig", "Veloc", "Mod", "Aft", "Bend"
};
const char* const midi_channels[17] = {
  "Off", " 1", " 2", " 3", " 4", " 5", " 6", " 7", " 8", " 9", "10", "11", "12", "13", "14", "15", "16"
};

SETTINGS_DECLARE(MIDIInterface, MIDI_SETTING_LAST) {
    { 1, 0, 5, "1 > MIDI", midi_out_functions, settings::STORAGE_TYPE_U8 },
    { 1, 0, 16, "  Channel", midi_channels, settings::STORAGE_TYPE_U8 },
    { 2, 0, 5, "2 > MIDI", midi_out_functions, settings::STORAGE_TYPE_U8 },
    { 1, 0, 16, "  Channel", midi_channels, settings::STORAGE_TYPE_U8 },
    { 0, 0, 5, "3 > MIDI", midi_out_functions, settings::STORAGE_TYPE_U8 },
    { 1, 0, 16, "  Channel", midi_channels, settings::STORAGE_TYPE_U8 },
    { 0, 0, 5, "4 > MIDI", midi_out_functions, settings::STORAGE_TYPE_U8 },
    { 1, 0, 16, "  Channel", midi_channels, settings::STORAGE_TYPE_U8 },

    { 1, 0, 6, "MIDI > A", midi_in_functions, settings::STORAGE_TYPE_U8 },
    { 1, 0, 16, "  Channel", midi_channels, settings::STORAGE_TYPE_U8 },
    { 2, 0, 6, "MIDI > B", midi_in_functions, settings::STORAGE_TYPE_U8 },
    { 1, 0, 16, "  Channel", midi_channels, settings::STORAGE_TYPE_U8 },
    { 3, 0, 6, "MIDI > C", midi_in_functions, settings::STORAGE_TYPE_U8 },
    { 1, 0, 16, "  Channel", midi_channels, settings::STORAGE_TYPE_U8 },
    { 4, 0, 6, "MIDI > D", midi_in_functions, settings::STORAGE_TYPE_U8 },
    { 1, 0, 16, "  Channel", midi_channels, settings::STORAGE_TYPE_U8 },

    { 1, 0, 5, "1 > MIDI", midi_out_functions, settings::STORAGE_TYPE_U8 },
    { 10, 0, 16, "  Channel", midi_channels, settings::STORAGE_TYPE_U8 },
    { 1, 0, 5, "2 > MIDI", midi_out_functions, settings::STORAGE_TYPE_U8 },
    { 11, 0, 16, "  Channel", midi_channels, settings::STORAGE_TYPE_U8 },
    { 1, 0, 5, "3 > MIDI", midi_out_functions, settings::STORAGE_TYPE_U8 },
    { 12, 0, 16, "  Channel", midi_channels, settings::STORAGE_TYPE_U8 },
    { 1, 0, 5, "4 > MIDI", midi_out_functions, settings::STORAGE_TYPE_U8 },
    { 13, 0, 16, "  Channel", midi_channels, settings::STORAGE_TYPE_U8 },

    { 3, 0, 6, "MIDI > A", midi_in_functions, settings::STORAGE_TYPE_U8 },
    { 10, 0, 16, "  Channel", midi_channels, settings::STORAGE_TYPE_U8 },
    { 4, 0, 6, "MIDI > B", midi_in_functions, settings::STORAGE_TYPE_U8 },
    { 10, 0, 16, "  Channel", midi_channels, settings::STORAGE_TYPE_U8 },
    { 5, 0, 6, "MIDI > C", midi_in_functions, settings::STORAGE_TYPE_U8 },
    { 10, 0, 16, "  Channel", midi_channels, settings::STORAGE_TYPE_U8 },
    { 6, 0, 6, "MIDI > D", midi_in_functions, settings::STORAGE_TYPE_U8 },
    { 10, 0, 16, "  Channel", midi_channels, settings::STORAGE_TYPE_U8 },

    { 0, -24, 24, "1 > MIDI", NULL, settings::STORAGE_TYPE_I8 },
    { 0, -24, 24, "2 > MIDI", NULL, settings::STORAGE_TYPE_I8 },
    { 0, -24, 24, "3 > MIDI", NULL, settings::STORAGE_TYPE_I8 },
    { 0, -24, 24, "4 > MIDI", NULL, settings::STORAGE_TYPE_I8 },
    { 0, -24, 24, "MIDI > A", NULL, settings::STORAGE_TYPE_I8 },
    { 0, -24, 24, "MIDI > B", NULL, settings::STORAGE_TYPE_I8 },
    { 0, -24, 24, "MIDI > C", NULL, settings::STORAGE_TYPE_I8 },
    { 0, -24, 24, "MIDI > D", NULL, settings::STORAGE_TYPE_I8 },

    { 0, -24, 24, "1 > MIDI", NULL, settings::STORAGE_TYPE_I8 },
    { 0, -24, 24, "2 > MIDI", NULL, settings::STORAGE_TYPE_I8 },
    { 0, -24, 24, "3 > MIDI", NULL, settings::STORAGE_TYPE_I8 },
    { 0, -24, 24, "4 > MIDI", NULL, settings::STORAGE_TYPE_I8 },
    { 0, -24, 24, "MIDI > A", NULL, settings::STORAGE_TYPE_I8 },
    { 0, -24, 24, "MIDI > B", NULL, settings::STORAGE_TYPE_I8 },
    { 0, -24, 24, "MIDI > C", NULL, settings::STORAGE_TYPE_I8 },
    { 0, -24, 24, "MIDI > D", NULL, settings::STORAGE_TYPE_I8 },

    { 0, 0, 1, "Setup", NULL, settings::STORAGE_TYPE_U8 }
};

MIDIInterface midi_instance;

// App stubs
void MIDI_init() {
    midi_instance.Start();
}

size_t MIDI_storageSize() {
    return MIDIInterface::storageSize();
}

size_t MIDI_save(void *storage) {
    return midi_instance.Save(storage);
}

size_t MIDI_restore(const void *storage) {
    size_t s = midi_instance.Restore(storage);
    midi_instance.Resume();
    return s;
}

void MIDI_isr() {
	return midi_instance.Controller();
}

void MIDI_handleAppEvent(OC::AppEvent event) {

}

void MIDI_loop() {
}

void MIDI_menu() {
    midi_instance.View();
}

void MIDI_screensaver() {
    MIDI_menu();
}

void MIDI_handleButtonEvent(const UI::Event &event) {
    if (event.control == OC::CONTROL_BUTTON_R && event.type == UI::EVENT_BUTTON_PRESS)
        midi_instance.cursor.toggle_editing();
    if (event.control == OC::CONTROL_BUTTON_L && event.type == UI::EVENT_BUTTON_PRESS)
        midi_instance.Panic();
    if (event.control == OC::CONTROL_BUTTON_UP) midi_instance.SelectSetup(0);
    if (event.control == OC::CONTROL_BUTTON_DOWN) midi_instance.SelectSetup(1);
}

void MIDI_handleEncoderEvent(const UI::Event &event) {
    if (event.control == OC::CONTROL_ENCODER_R) {
        if (midi_instance.cursor.editing()) {
            midi_instance.change_value(midi_instance.cursor.cursor_pos(), event.value);
        } else {
            midi_instance.cursor.Scroll(event.value);
        }
    }
    if (event.control == OC::CONTROL_ENCODER_L) {
        midi_instance.SwitchScreen(); // Switch from Assign to Note and back
    }
}
