//=============================================================================
//
// Adventure Game Studio (AGS)
//
// Copyright (C) 1999-2011 Chris Jones and 2011-20xx others
// The full list of copyright holders can be found in the Copyright.txt
// file, which is part of this source code distribution.
//
// The AGS source code is provided under the Artistic License 2.0.
// A copy of this license can be found in the file License.txt and at
// http://www.opensource.org/licenses/artistic-license-2.0.php
//
//=============================================================================

#include <stdio.h>
#include "ac/common.h"
#include "ac/file.h"
#include "ac/game.h"
#include "ac/global_audio.h"
#include "ac/lipsync.h"
#include "debug/debug_log.h"
#include "debug/debugger.h"
#include "game/game_objects.h"
#include "media/audio/audio.h"
#include "media/audio/sound.h"

extern char *speech_file;
extern SpeechLipSyncLine *splipsync;
extern int numLipLines, curLipLine, curLipLinePhenome;

void StopAmbientSound (int channel) {
    if ((channel < 0) || (channel >= MAX_SOUND_CHANNELS))
        quit("!StopAmbientSound: invalid channel");

    if (ambient[channel].channel == 0)
        return;

    stop_and_destroy_channel(channel);
    ambient[channel].channel = 0;
}

void PlayAmbientSound (int channel, int sndnum, int vol, int x, int y) {
    // the channel parameter is to allow multiple ambient sounds in future
    if ((channel < 1) || (channel == SCHAN_SPEECH) || (channel >= MAX_SOUND_CHANNELS))
        quit("!PlayAmbientSound: invalid channel number");
    if ((vol < 1) || (vol > 255))
        quit("!PlayAmbientSound: volume must be 1 to 255");

    if (usetup.DigitalSoundCard == DIGI_NONE)
        return;

    // only play the sound if it's not already playing
    if ((ambient[channel].channel < 1) || (channels[ambient[channel].channel] == NULL) ||
        (channels[ambient[channel].channel]->done == 1) ||
        (ambient[channel].num != sndnum)) {

            StopAmbientSound(channel);
            // in case a normal non-ambient sound was playing, stop it too
            stop_and_destroy_channel(channel);

            SOUNDCLIP *asound = load_sound_from_path(sndnum, vol, true);

            if (asound == NULL) {
                debug_log ("Cannot load ambient sound %d", sndnum);
                DEBUG_CONSOLE("FAILED to load ambient sound %d", sndnum);
                return;
            }

            DEBUG_CONSOLE("Playing ambient sound %d on channel %d", sndnum, channel);
            ambient[channel].channel = channel;
            channels[channel] = asound;
            channels[channel]->priority = 15;  // ambient sound higher priority than normal sfx
    }
    // calculate the maximum distance away the player can be, using X
    // only (since X centred is still more-or-less total Y)
    ambient[channel].maxdist = ((x > thisroom.Width / 2) ? x : (thisroom.Width - x)) - AMBIENCE_FULL_DIST;
    ambient[channel].num = sndnum;
    ambient[channel].x = x;
    ambient[channel].y = y;
    ambient[channel].vol = vol;
    update_ambient_sound_vol();
}

int IsChannelPlaying(int chan) {
    if (play.FastForwardCutscene)
        return 0;

    if ((chan < 0) || (chan >= MAX_SOUND_CHANNELS))
        quit("!IsChannelPlaying: invalid sound channel");

    if ((channels[chan] != NULL) && (channels[chan]->done == 0))
        return 1;

    return 0;
}

int IsSoundPlaying() {
    if (play.FastForwardCutscene)
        return 0;

    // find if there's a sound playing
    for (int i = SCHAN_NORMAL; i < numSoundChannels; i++) {
        if ((channels[i] != NULL) && (channels[i]->done == 0))
            return 1;
    }

    return 0;
}

// returns -1 on failure, channel number on success
int PlaySoundEx(int val1, int channel) {

    if (debug_flags & DBG_NOSFX)
        return -1;

    // if no sound, ignore it
    if (usetup.DigitalSoundCard == DIGI_NONE)
        return -1;

    if ((channel < SCHAN_NORMAL) || (channel >= MAX_SOUND_CHANNELS))
        quit("!PlaySoundEx: invalid channel specified, must be 3-7");

    // if an ambient sound is playing on this channel, abort it
    StopAmbientSound(channel);

    if (val1 < 0) {
        stop_and_destroy_channel (channel);
        return -1;
    }
    // if skipping a cutscene, don't try and play the sound
    if (play.FastForwardCutscene)
        return -1;

    // that sound is already in memory, play it
    if (!psp_audio_multithreaded)
    {
        if ((last_sound_played[channel] == val1) && (channels[channel] != NULL)) {
            DEBUG_CONSOLE("Playing sound %d on channel %d; cached", val1, channel);
            channels[channel]->restart();
            channels[channel]->set_volume (play.SoundVolume);
            return channel;
        }
    }

    // free the old sound
    stop_and_destroy_channel (channel);
    DEBUG_CONSOLE("Playing sound %d on channel %d", val1, channel);

    last_sound_played[channel] = val1;

    SOUNDCLIP *soundfx = load_sound_from_path(val1, play.SoundVolume, 0);

    if (soundfx == NULL) {
        debug_log("Sound sample load failure: cannot load sound %d", val1);
        DEBUG_CONSOLE("FAILED to load sound %d", val1);
        return -1;
    }

    channels[channel] = soundfx;
    channels[channel]->priority = 10;
    channels[channel]->set_volume (play.SoundVolume);
    return channel;
}

void StopAllSounds(int evenAmbient) {
    // backwards-compatible hack -- stop Type 3 (default Sound Type)
    Game_StopAudio(3);

    if (evenAmbient)
        Game_StopAudio(1);
}

void PlayMusicResetQueue(int newmus) {
    play.MusicQueueLength = 0;
    newmusic(newmus);
}

void SeekMIDIPosition (int position) {
    if (play.SilentMidiIndex)
        midi_seek (position);
    if (current_music_type == MUS_MIDI) {
        midi_seek(position);
        DEBUG_CONSOLE("Seek MIDI position to %d", position);
    }
}

int GetMIDIPosition () {
    if (play.SilentMidiIndex)
        return midi_pos;
    if (current_music_type != MUS_MIDI)
        return -1;
    if (play.FastForwardCutscene)
        return 99999;

    return midi_pos;
}

int IsMusicPlaying() {
    // in case they have a "while (IsMusicPlaying())" loop
    if ((play.FastForwardCutscene) && (play.SkipUntilCharacterStops < 0))
        return 0;

    if (usetup.MidiSoundCard == MIDI_NONE)
        return 0;

    if (current_music_type != 0) {
        if (channels[SCHAN_MUSIC] == NULL)
            current_music_type = 0;
        else if (channels[SCHAN_MUSIC]->done == 0)
            return 1;
        else if ((crossFading > 0) && (channels[crossFading] != NULL))
            return 1;
        return 0;
    }

    return 0;
}

int PlayMusicQueued(int musnum) {

    // Just get the queue size
    if (musnum < 0)
        return play.MusicQueueLength;

    if ((IsMusicPlaying() == 0) && (play.MusicQueueLength == 0)) {
        newmusic(musnum);
        return 0;
    }

    if (play.MusicQueueLength >= MAX_QUEUED_MUSIC) {
        DEBUG_CONSOLE("Too many queued music, cannot add %d", musnum);
        return 0;
    }

    if ((play.MusicQueueLength > 0) && 
        (play.MusicQueue[play.MusicQueueLength - 1] >= QUEUED_MUSIC_REPEAT)) {
            quit("!PlayMusicQueued: cannot queue music after a repeating tune has been queued");
    }

    if (play.MusicLoopMode) {
        DEBUG_CONSOLE("Queuing music %d to loop", musnum);
        musnum += QUEUED_MUSIC_REPEAT;
    }
    else {
        DEBUG_CONSOLE("Queuing music %d", musnum);
    }

    play.MusicQueue[play.MusicQueueLength] = musnum;
    play.MusicQueueLength++;

    if (play.MusicQueueLength == 1) {

        clear_music_cache();

        cachedQueuedMusic = load_music_from_disk(musnum, (play.MusicLoopMode > 0));
    }

    return play.MusicQueueLength;
}

void scr_StopMusic() {
    play.MusicQueueLength = 0;
    stopmusic();
}

void SeekMODPattern(int patnum) {
    if (current_music_type == MUS_MOD && channels[SCHAN_MUSIC]) {
        channels[SCHAN_MUSIC]->seek (patnum);
        DEBUG_CONSOLE("Seek MOD/XM to pattern %d", patnum);
    }
}
void SeekMP3PosMillis (int posn) {
    if (current_music_type) {
        DEBUG_CONSOLE("Seek MP3/OGG to %d ms", posn);
        if (crossFading && channels[crossFading])
            channels[crossFading]->seek (posn);
        else if (channels[SCHAN_MUSIC])
            channels[SCHAN_MUSIC]->seek (posn);
    }
}

int GetMP3PosMillis () {
    // in case they have "while (GetMP3PosMillis() < 5000) "
    if (play.FastForwardCutscene)
        return 999999;

    if (current_music_type && channels[SCHAN_MUSIC]) {
        int result = channels[SCHAN_MUSIC]->get_pos_ms();
        if (result >= 0)
            return result;

        return channels[SCHAN_MUSIC]->get_pos ();
    }

    return 0;
}

void SetMusicVolume(int newvol) {
    if ((newvol < kRoomVolumeMin) || (newvol > kRoomVolumeMax))
        quitprintf("!SetMusicVolume: invalid volume number. Must be from %d to %d.", kRoomVolumeMin, kRoomVolumeMax);
    thisroom.Options[kRoomBaseOpt_MusicVolume]=newvol;
    update_music_volume();
}

void SetMusicMasterVolume(int newvol) {
    const int min_volume = loaded_game_file_version < kGameVersion_330 ? 0 :
        -LegacyMusicMasterVolumeAdjustment - (kRoomVolumeMax * LegacyRoomVolumeFactor);
    if ((newvol < min_volume) | (newvol>100))
        quitprintf("!SetMusicMasterVolume: invalid volume - must be from %d to %d", min_volume, 100);
    play.music_master_volume=newvol+LegacyMusicMasterVolumeAdjustment;
    update_music_volume();
}

void SetSoundVolume(int newvol) {
    if ((newvol<0) | (newvol>255))
        quit("!SetSoundVolume: invalid volume - must be from 0-255");
    play.SoundVolume = newvol;
    Game_SetAudioTypeVolume(AUDIOTYPE_LEGACY_AMBIENT_SOUND, (newvol * 100) / 255, VOL_BOTH);
    Game_SetAudioTypeVolume(AUDIOTYPE_LEGACY_SOUND, (newvol * 100) / 255, VOL_BOTH);
    update_ambient_sound_vol ();
}

void SetChannelVolume(int chan, int newvol) {
    if ((newvol<0) || (newvol>255))
        quit("!SetChannelVolume: invalid volume - must be from 0-255");
    if ((chan < 0) || (chan >= MAX_SOUND_CHANNELS))
        quit("!SetChannelVolume: invalid channel id");

    if ((channels[chan] != NULL) && (channels[chan]->done == 0)) {
        if (chan == ambient[chan].channel) {
            ambient[chan].vol = newvol;
            update_ambient_sound_vol();
        }
        else
            channels[chan]->set_volume (newvol);
    }
}

void SetDigitalMasterVolume (int newvol) {
    if ((newvol<0) | (newvol>100))
        quit("!SetDigitalMasterVolume: invalid volume - must be from 0-100");
    play.DigitalMasterVolume = newvol;
    set_volume ((newvol * 255) / 100, -1);
}

int GetCurrentMusic() {
    return play.CurrentMusicIndex;
}

void SetMusicRepeat(int loopflag) {
    play.MusicLoopMode=loopflag;
}

void PlayMP3File (const char *filename) {
    if (strlen(filename) >= PLAYMP3FILE_MAX_FILENAME_LEN)
        quit("!PlayMP3File: filename too long");

    DEBUG_CONSOLE("PlayMP3File %s", filename);

    char pathToFile[MAX_PATH];
    get_current_dir_path(pathToFile, filename);

    int useChan = prepare_for_new_music ();
    bool doLoop = (play.MusicLoopMode > 0);

    if ((channels[useChan] = my_load_static_ogg(pathToFile, 150, doLoop)) != NULL) {
        channels[useChan]->play();
        current_music_type = MUS_OGG;
        play.CurrentMusicIndex = 1000;
        play.PlayMp3FileName = filename;
    }
    else if ((channels[useChan] = my_load_static_mp3(pathToFile, 150, doLoop)) != NULL) {
        channels[useChan]->play();
        current_music_type = MUS_MP3;
        play.CurrentMusicIndex = 1000;
        play.PlayMp3FileName = filename;
    }
    else
        debug_log ("PlayMP3File: file '%s' not found or cannot play", filename);

    post_new_music_check(useChan);

    update_music_volume();
}

void PlaySilentMIDI (int mnum) {
    if (current_music_type == MUS_MIDI)
        quit("!PlaySilentMIDI: proper midi music is in progress");

    set_volume (-1, 0);
    play.SilentMidiIndex = mnum;
    play.SilentMidiChannel = SCHAN_SPEECH;
    stop_and_destroy_channel(play.SilentMidiChannel);
    channels[play.SilentMidiChannel] = load_sound_clip_from_old_style_number(true, mnum, false);
    if (channels[play.SilentMidiChannel] == NULL)
    {
        quitprintf("!PlaySilentMIDI: failed to load aMusic%d", mnum);
    }
    channels[play.SilentMidiChannel]->play();
    channels[play.silent_midi_channel]->set_volume_origin(0);
}

void SetSpeechVolume(int newvol) {
    if ((newvol<0) | (newvol>255))
        quit("!SetSpeechVolume: invalid volume - must be from 0-255");

    if (channels[SCHAN_SPEECH])
        channels[SCHAN_SPEECH]->set_volume (newvol);

    play.SpeechVolume = newvol;
}

void __scr_play_speech(int who, int which) {
    // *** implement this - needs to call stop_speech as well
    // to reset the volume
    quit("PlaySpeech not yet implemented");
}

// 0 = text only
// 1 = voice & text
// 2 = voice only
void SetVoiceMode (int newmod) {
    if ((newmod < 0) | (newmod > 2))
        quit("!SetVoiceMode: invalid mode number (must be 0,1,2)");
    // If speech is turned off, store the mode anyway in case the
    // user adds the VOX file later
    if (play.SpeechVoiceMode < 0)
        play.SpeechVoiceMode = (-newmod) - 1;
    else
        play.SpeechVoiceMode = newmod;
}

int GetVoiceMode()
{
    return play.SpeechVoiceMode >= 0 ? play.SpeechVoiceMode : (-play.SpeechVoiceMode + 1);
}

int IsVoxAvailable() {
    if (play.SpeechVoiceMode < 0)
        return 0;
    return 1;
}

int IsMusicVoxAvailable () {
    return play.UseSeparateMusicLib;
}

int play_speech(int charid,int sndid) {
    stop_and_destroy_channel (SCHAN_SPEECH);

    // don't play speech if we're skipping a cutscene
    if (play.FastForwardCutscene)
        return 0;
    if ((play.SpeechVoiceMode < 1) || (speech_file == NULL))
        return 0;

    SOUNDCLIP *speechmp3;
    /*  char finame[40]="~SPEECH.VOX~NARR";
    if (charid >= 0)
    strncpy(&finame[12],game.Characters[charid].scrname,4);*/

    char finame[40] = "~";
    strcat(finame, get_filename(speech_file));
    strcat(finame, "~");

    if (charid >= 0) {
        // append the first 4 characters of the script name to the filename
        char theScriptName[5];
        if (game.Characters[charid].scrname[0] == 'c')
            strncpy(theScriptName, &game.Characters[charid].scrname[1], 4);
        else
            strncpy(theScriptName, game.Characters[charid].scrname, 4);
        theScriptName[4] = 0;
        strcat(finame, theScriptName);
    }
    else
        strcat(finame, "NARR");

    // append the speech number
    sprintf(&finame[strlen(finame)],"%d",sndid);

    int ii;  // Compare the base file name to the .pam file name
    char *basefnptr = strchr (&finame[4], '~') + 1;
    curLipLine = -1;  // See if we have voice lip sync for this line
    curLipLinePhenome = -1;
    for (ii = 0; ii < numLipLines; ii++) {
        if (stricmp(splipsync[ii].filename, basefnptr) == 0) {
            curLipLine = ii;
            break;
        }
    }
    // if the lip-sync is being used for voice sync, disable
    // the text-related lipsync
    if (numLipLines > 0)
        game.Options[OPT_LIPSYNCTEXT] = 0;

    strcat (finame, ".WAV");
    speechmp3 = my_load_wave (finame, play.SpeechVolume, 0);

    if (speechmp3 == NULL) {
        strcpy (&finame[strlen(finame)-3], "ogg");
        speechmp3 = my_load_ogg (finame, play.SpeechVolume);
    }

    if (speechmp3 == NULL) {
        strcpy (&finame[strlen(finame)-3], "mp3");
        speechmp3 = my_load_mp3 (finame, play.SpeechVolume);
    }

    if (speechmp3 != NULL) {
        if (speechmp3->play() == 0)
            speechmp3 = NULL;
    }

    if (speechmp3 == NULL) {
        debug_log ("Speech load failure: '%s'",finame);
        curLipLine = -1;
        return 0;
    }

    channels[SCHAN_SPEECH] = speechmp3;
    play.MusicVolumeWas = play.MusicMasterVolume;

    // Negative value means set exactly; positive means drop that amount
    if (play.MusicMuteForVoicePlay < 0)
        play.MusicMasterVolume = -play.MusicMuteForVoicePlay;
    else
        play.MusicMasterVolume -= play.MusicMuteForVoicePlay;

    apply_volume_drop_modifier(true);
    update_music_volume();
    update_music_at = 0;
    mvolcounter = 0;

    update_ambient_sound_vol();

    // change Sierra w/bgrnd  to Sierra without background when voice
    // is available (for Tierra)
    if ((game.Options[OPT_SPEECHTYPE] == 2) && (play.NoTextBkgForVoiceSpeech > 0)) {
        game.Options[OPT_SPEECHTYPE] = 1;
        play.NoTextBkgForVoiceSpeech = 2;
    }

    return 1;
}

void stop_speech() {
    if (channels[SCHAN_SPEECH] != NULL) {
        play.MusicMasterVolume = play.MusicVolumeWas;
        // update the music in a bit (fixes two speeches follow each other
        // and music going up-then-down)
        update_music_at = 20;
        mvolcounter = 1;
        stop_and_destroy_channel (SCHAN_SPEECH);
        curLipLine = -1;

        if (play.NoTextBkgForVoiceSpeech == 2) {
            // set back to Sierra w/bgrnd
            play.NoTextBkgForVoiceSpeech = 1;
            game.Options[OPT_SPEECHTYPE] = 2;
        }
    }
}
