#include "jdkmidihandler.h"

namespace SoS
{
	namespace Core
	{

		JdkMidiHandler::JdkMidiHandler(Song* song, ISongSettings* settings) :
			song(song),
			settings(settings),
			multiTrack(new MIDIMultiTrack()),
			driver(new MIDIDriverWin32 ( 128 ))
		{
			manager = new MIDIManager ( driver, NULL );
			sequencer = new MIDISequencer ( multiTrack, NULL );

			manager->SetSeq ( sequencer );
			driver->StartTimer ( SOS_TIME_RESOLUTION );
			driver->OpenMIDIOutPort ( MIDI_MAPPER );
			manager->SetSeq ( sequencer );
		}

		JdkMidiHandler::~JdkMidiHandler()
		{
			manager->SeqStop();
			driver->AllNotesOff();
			driver->StopTimer();

			delete driver;
			delete sequencer;
			delete manager;
			delete multiTrack;
		}

		void JdkMidiHandler::rewind()
		{
			moveTo(settings->getCurrentTime());
		}

		void JdkMidiHandler::moveTo(long time)
		{
			reset();
			double next_event_time = 0.0;
			int ev_track;
			MIDITimedBigMessage ev;

			driver->StopTimer();
			sequencer->GoToZero();

			while ( sequencer->GetNextEventTimeMs ( &next_event_time )
				&& next_event_time < time
				&& sequencer->GetNextEvent ( &ev_track, &ev )
			)
			{
				if(!ev.IsNote())
					driver->HardwareMsgOut(ev);
			}

			changeVolume();
			manager->SetCurrTime(time);
			driver->StartTimer(SOS_TIME_RESOLUTION);
		}

		void JdkMidiHandler::startPlaying()
		{
			rewind();
			manager->SetTimeOffset ( timeGetTime() - settings->getCurrentTime());
			manager->SetSeqOffset(settings->getOutputLatency());
			manager->SeqPlay();
		}

		void JdkMidiHandler::stopPlaying()
		{
			manager->SeqStop();
			driver->AllNotesOff();
		}

		bool JdkMidiHandler::isPlaying() const
		{
			return manager->IsSeqPlay();
		}

		long JdkMidiHandler::getCurrTime() const
		{
			return manager->GetCurrTime();
		}

		void JdkMidiHandler::setSolo(bool solo, int track)
		{
			driver->AllNotesOff();
			sequencer->SetSoloMode(solo, track);
		}

		void JdkMidiHandler::setMute(bool mute, int track)
		{
			driver->AllNotesOff();
			sequencer->GetTrackProcessor(track)->mute = mute;
		}

		void JdkMidiHandler::changeVolume()
		{
			for(int i = 0; i < 16; i++)
			{
				MIDITimedBigMessage msg;
				msg.SetControlChange(i, C_MAIN_VOLUME, settings->getVolume() / 2);
				driver->HardwareMsgOut(msg);
			}
		}

		void JdkMidiHandler::free()
		{

		}

		void JdkMidiHandler::changeKey()
		{
			if(sequencer->GetTrackProcessor(0)->transpose != settings->getKeyShift())
			{
				for(int i = 0; i < sequencer->GetNumTracks(); i++)
				{
					sequencer->GetTrackProcessor(i)->transpose = settings->getKeyShift();
				}
				driver->AllNotesOff();
			}
		}

		void JdkMidiHandler::changeTempo()
		{
			manager->SetCurrTime(settings->getCurrentTime());
			sequencer->SetCurrentTempoScale(settings->getTempo());
		}

		void JdkMidiHandler::updateTrackPlayedTime()
		{
			for(int i = 0; i < multiTrack->GetNumTracks(); i ++)
			{
				if(!multiTrack->GetTrack(i)->IsTrackEmpty() && i < song->tracks.size())
				{
					unsigned long playedMsAgo = timeGetTime() - sequencer->GetTrackState(i)->lastNoteOnSysTime;
					song->tracks[i]->volume = playedMsAgo > 1000 ? 0 : 16*(1000 - playedMsAgo)/1000;
				}
			}
		}

		void JdkMidiHandler::reset()
		{
			driver->AllNotesOff();
			driver->Reset();
			driver->ResetMIDIOut();

			for(int i = 0; i < 16; i++)
			{
				MIDITimedBigMessage msg;
				msg.SetControlChange(i, C_RESET, 0);
				driver->HardwareMsgOut(msg);
			}
		}

		bool JdkMidiHandler::loadMusic()
		{
            MIDIFileReadStreamFile rs ( (song->properties[SOS_SONG_PROP_PATH] + song->properties[SOS_SONG_PROP_MIDIFILENAME]).c_str() );
			MIDIFileReadMultiTrack track_loader ( multiTrack );
			MIDIFileRead reader ( &rs, &track_loader );

			if(!rs.IsValid())
				return false;

			reset();
			multiTrack->Clear();
			sequencer->SetCurrentTempoScale(1.0);
			sequencer->SetSoloMode(false);
			for(int i = 0; i < sequencer->GetNumTracks(); i++)
				sequencer->GetTrackProcessor(i)->mute = false;

			if(!reader.Parse())
			{
                FILE_LOG(logWARNING) << "[JdkMidiHandler]: File \"" << (song->properties[SOS_SONG_PROP_PATH] + song->properties[SOS_SONG_PROP_MIDIFILENAME]).c_str() << "\" could not be parsed!";
				return false;
			}

			handleSongLoaded();
			return true;

		}

		void JdkMidiHandler::handleSongLoaded()
		{
			sequencer->GoToZero();
			MIDITimedBigMessage cmpMsg;
			int trackNum = -1;

			for(int i = 0; i < multiTrack->GetNumTracks(); i++)
			{
				song->tracks.push_back(new Track());
			}

			while(sequencer->GetNextEvent(&trackNum, &cmpMsg))
			{
				Track* track = song->tracks[trackNum];

				if(cmpMsg.IsNote())
				{
					if(cmpMsg.ImplicitIsNoteOn())
					{
						Note* note = new Note();
						note->startTime = sequencer->GetCurrentTimeInMs();
						note->notePitch = cmpMsg.GetNote();

						if(note->notePitch > track->topNote)
							track->topNote = note->notePitch;
						if(note->notePitch < track->bottomNote)
							track->bottomNote = note->notePitch;

						track->notes.push_back(note);
					}
					else if(cmpMsg.ImplicitIsNoteOff())
					{
						for (int i = 0; i < track->notes.size(); i++)
						{
							Note* note = track->notes[i];
							if(note->notePitch == cmpMsg.GetNote() && note->stopTime == 0)
							{
								note->stopTime = sequencer->GetCurrentTimeInMs();
								break;
							}
						}
					}
				}
				else if(cmpMsg.IsTextEvent())
				{
					TextData* textData = new TextData();

					std::stringstream wss;
					wss << cmpMsg.GetSysExString().c_str();

					textData->text = cmpMsg.GetSysExString();
					textData->time = sequencer->GetCurrentTimeInMs();
					textData->type = (ITextData::TextDataType)cmpMsg.GetMetaType();
					if(textData->time > 0 && textData->text.find('@') != 0)
					{
						textData->type =  ITextData::LYRIC_TEXT;
					}
					if(textData->type < ITextData::GENERIC_TEXT || textData->type > ITextData::GENERIC_TEXT_F)
						textData->type = ITextData::GENERIC_TEXT;

					textData->lineNo = track->textData[textData->type].size() > 0 ? track->textData[textData->type].back()->lineNo : 0;
					textData->paragraphNo = track->textData[textData->type].size() > 0 ? track->textData[textData->type].back()->paragraphNo : 0;

					if((textData->text.find('/') != std::string::npos || textData->text.find('\n') != std::string::npos ||
						textData->text.find('\r') != std::string::npos )
						&& track->textData[ITextData::LYRIC_TEXT].size() > 0)
						textData->lineNo++;

					if(textData->text.find('\\') != std::string::npos && track->textData[ITextData::LYRIC_TEXT].size() > 0)
					{
						textData->lineNo++;
						textData->paragraphNo++;
					}

					if(cmpMsg.GetMetaType() == META_TRACK_NAME);
						track->name = textData->text;

					if(trackNum == 0 && cmpMsg.GetMetaType() != META_COPYRIGHT && cmpMsg.GetMetaType() != META_LYRIC_TEXT)
                        song->properties[SOS_SONG_PROP_DESCRIPTION] += textData->text + " ";

					track->textData[textData->type].push_back(textData);
				}
				else
				{
					driver->HardwareMsgOut(cmpMsg);
				}
			}

			song->clearEmptyTracks();
			song->songDuration = sequencer->GetMisicDurationInSeconds() * 1000;
			sequencer->GoToZero();
		}
	}
}
