/*  ===========================================================================
*
*   This file is part of HISE.
*   Copyright 2016 Christoph Hart
*
*   HISE is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   HISE is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with HISE.  If not, see <http://www.gnu.org/licenses/>.
*
*   Commercial licenses for using HISE in an closed source project are
*   available on request. Please visit the project's website to get more
*   information about commercial licensing:
*
*   http://www.hise.audio/
*
*   HISE is based on the JUCE library,
*   which must be separately licensed for closed source applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

namespace hise { using namespace juce;

MidiProcessor::MidiProcessor(MainController *mc, const String &id):
		Processor(mc, id, 1),
		processThisMessage(true),
		ownerSynth(nullptr),
		numThisTime(0)
	{

		
		

	};

MidiProcessor::~MidiProcessor()
{
	ownerSynth = nullptr;
	masterReference.clear();
};

#if USE_BACKEND
struct MidiProcessor::EventLogger
{
    EventLogger():
      inputQueue(512),
	  outputQueue(512)
    {};
    
    struct Display: public Component,
                    public PooledUIUpdater::SimpleTimer,
					public TextEditor::Listener
    {
        static constexpr int RowHeight = 24;
		static constexpr int TopHeight = 54;
		static constexpr int HeaderHeight = 30;
        
        enum class Columns
        {
            Type,
            Ignored,
            Artificial,
			Channel,
            Number,
            Value,
			TransposeAmount,
			FadeTime,
			CoarseDetune,
			FineDetune,
            Timestamp,
            EventId,
            numColumns
        };

		bool columnStates[(int)Columns::numColumns];

		static constexpr int NumFixColumns = 3;
        
        Display(MidiProcessor* mp_, EventLogger* l):
          SimpleTimer(mp_->getMainController()->getGlobalUIUpdater()),
          resizer(this, nullptr),
		  filter(),
          mp(mp_),
          logger(l),
		  filterResult(Result::ok()),
		  processButton("process", nullptr, f, "bypass"),
		  clearButton("processing-setup", nullptr, f)
        {
			for (int i = 0; i < (int)Columns::numColumns; i++)
				columnStates[i] = true;

			columnStates[(int)Columns::TransposeAmount] = false;
			columnStates[(int)Columns::CoarseDetune] = false;
			columnStates[(int)Columns::FineDetune] = false;
			columnStates[(int)Columns::FadeTime] = false;
			columnStates[(int)Columns::EventId] = false;
			columnStates[(int)Columns::Timestamp] = false;

            addAndMakeVisible(resizer);
			addAndMakeVisible(filter);
			addAndMakeVisible(processButton);
			addAndMakeVisible(clearButton);

			clearButton.onClick = [this]() { allInputEvents.clear(); allOutputEvents.clear();  rebuildEventsToShow(); };

			clearButton.setTooltip("Clear the event list");

			filter.setTooltip("Filter the list with a HiseScript expression (eg. Message.getNoteNumber() > 64)");
			filter.setReturnKeyStartsNewLine(false);

			processButton.setToggleModeWithColourChange(true);
			processButton.setToggleStateAndUpdateIcon(true);
			processButton.onClick = BIND_MEMBER_FUNCTION_0(Display::rebuildEventsToShow);
			processButton.setTooltip("Show events after processing");

			if (auto jsp = dynamic_cast<JavascriptMidiProcessor*>(mp_))
			{
				if (jsp->isDeferred())
				{
					processButton.setToggleStateAndUpdateIcon(false);
					processButton.setEnabled(false);
					processButton.setTooltip(jsp->getId() + " is deferred");
				}
			}

			filter.addListener(this);

			GlobalHiseLookAndFeel::setTextEditorColours(filter);
			filter.setTextToShowWhenEmpty("Filter events", Colours::black.withAlpha(0.3f));

            start();
            setSize(400, TopHeight + 16 * RowHeight);
            setName("Event Logger: " + mp->getId());

			Random r;

			for (int i = 0; i < 32; i++)
				colours[i] = Colour(0xFFFFAAAA).withHue(r.nextFloat());

			engine = new JavascriptEngine();
			data = new DynamicObject();

			#define GET_PROPERTY(propertyId) args.thisObject.getProperty(propertyId, 0)
			#define SET_METHOD(methodName, expression) data->setMethod(methodName, [](const var::NativeFunctionArgs& args) { return expression; });

			SET_METHOD("getNoteNumber", GET_PROPERTY("number"));
			SET_METHOD("getChannel", GET_PROPERTY("channel"));
			SET_METHOD("getVelocity", GET_PROPERTY("velocity"));
			SET_METHOD("getControllerNumber", GET_PROPERTY("number"));
			SET_METHOD("getControllerValue", GET_PROPERTY("velocity"));
			SET_METHOD("getTimestamp", GET_PROPERTY("timestamp"));
			SET_METHOD("getEventId", GET_PROPERTY("event_id"));
			SET_METHOD("isArtificial", GET_PROPERTY("artificial"));
			SET_METHOD("isTimerEvent", GET_PROPERTY("timer"));
			SET_METHOD("isIgnored", GET_PROPERTY("ignored"));
			SET_METHOD("isNoteOn", (int)GET_PROPERTY("type") == (int)HiseEvent::Type::NoteOn);
			SET_METHOD("isNoteOff", (int)GET_PROPERTY("type") == (int)HiseEvent::Type::NoteOff);
			SET_METHOD("isController", (int)GET_PROPERTY("type") == (int)HiseEvent::Type::Controller);
			
			#undef GET_PROPERTY
			#undef SET_METHOD

			engine->registerNativeObject("Message", data.get());
        }

		~Display()
		{
			mp->setEnableEventLogger(false);
		}

		int getColumnWidth(Columns c)
		{
			if (!columnStates[(int)c])
				return 0;

			if ((int)c < NumFixColumns)
				return RowHeight;

			auto w = getWidth();

			for (int i = 0; i < NumFixColumns; i++)
			{
				if (columnStates[i])
					w -= RowHeight;
			}

			int numToShow = 0;

			for (int i = NumFixColumns; i < int(Columns::numColumns); i++)
				numToShow += (int)columnStates[i];

			auto dynamicWidth = w / jmax(1, numToShow);
			return dynamicWidth;
		}

		void mouseDown(const MouseEvent& e) override
		{
			if (e.mods.isRightButtonDown())
			{
				PopupLookAndFeel mlaf;
				PopupMenu m;
				m.setLookAndFeel(&mlaf);

				m.addSectionHeader("Show columns");

				for (int i = 0; i < (int)Columns::numColumns; i++)
					m.addItem(i + 1, getColumnName((Columns)i), true, columnStates[i]);

				if (auto r = m.show())
				{
					columnStates[r - 1] = !columnStates[r - 1];
					repaint();
				}
			}
		}

        void drawEventColumn(Graphics& g, const HiseEvent& e, Columns c, Rectangle<float> area)
        {
			auto hasNumberData = e.isNoteOnOrOff() || e.isController() || 
								 e.isAftertouch() || e.isPitchWheel() ||
								 e.isPitchFade() || e.isVolumeFade();

            g.setFont(GLOBAL_MONOSPACE_FONT());
            g.setColour(Colours::black.withAlpha(0.05f));
            g.fillRect(area.reduced(0.5f));
            g.setColour(Colours::white.withAlpha(e.isIgnored() ? 0.3f : 0.8f));
            
            auto draw = [&](int v, bool force=false)
            {
				if(hasNumberData || force)
					g.drawText(String(v), area, Justification::centred);
            };
            
            switch(c)
            {
                case Columns::Type:
                {
                    if(e.isAllNotesOff())
                    {
                        g.setColour(Colours::red.withSaturation(0.6f));
                        g.drawText("!", area, Justification::centred);
                    }
					else if (e.isTimerEvent())
					{
						g.drawText("T", area, Justification::centred);
					}
					else if (e.isController())
					{
						g.drawText("CC", area, Justification::centred);
					}
					else if (e.isPitchWheel())
					{
						g.drawText("PB", area, Justification::centred);
					}
					else if (e.isPitchFade())
					{
						g.setColour(getColourForEvent(e.getEventId()));
						g.drawText("PF", area, Justification::centred);
					}
					else if (e.isVolumeFade())
					{
						g.setColour(getColourForEvent(e.getEventId()));
						g.drawText("VF", area, Justification::centred);
					}
                    else if(e.isNoteOnOrOff())
                    {
                        Path p;
                        p.startNewSubPath(0.0f, 0.0f);
                        p.lineTo(1.0f, 0.0f);
                        p.lineTo(0.5f, 1.0f);
                        p.closeSubPath();
                        
                        Colour c[8];
                        
                        c[0] = Colours::red;
                        c[1] = Colours::violet;
                        c[2] = Colours::brown;
                        c[3] = Colours::green;
                        c[4] = Colours::pink;
                        c[5] = Colours::yellow;
                        c[6] = Colours::orange;
                        c[7] = Colours::blue;
                        
						g.setColour(getColourForEvent(e.getEventId()));
                        
                        if(e.isNoteOff())
                            p.applyTransform(AffineTransform::rotation(float_Pi));
                        
                        PathFactory::scalePath(p, area.reduced(7.0f));
                        
                        g.fillPath(p);
                    }

					break;
                }
                case Columns::Ignored: if(e.isIgnored()) g.fillEllipse(area.reduced(9)); break;
                case Columns::Artificial: if(e.isArtificial()) g.fillEllipse(area.reduced(9)); break;
                case Columns::Number: draw(e.getNoteNumber()); break;
                case Columns::Channel: draw(e.getChannel()); break;
                case Columns::Value: draw(e.getVelocity()); break;
				case Columns::TransposeAmount: draw(e.getTransposeAmount()); break;
                case Columns::Timestamp: draw(e.getTimeStamp(), true); break;
				case Columns::CoarseDetune: draw(e.getCoarseDetune(), true); break;
				case Columns::FineDetune: draw(e.getFineDetune(), true); break;
				case Columns::FadeTime: if(e.isVolumeFade() || e.isPitchFade()) draw(e.getFadeTime(), true); break;
                case Columns::EventId: if(e.isNoteOnOrOff()) draw(e.getEventId()); break;
                default: return;
            }
        }

		static String getColumnName(Columns c, bool shortName=false)
		{
			switch (c)
			{
			case Columns::Type:				return shortName ? "T" : "Type";
			case Columns::Ignored:			return shortName ? "I" : "Ignored";
			case Columns::Artificial:		return shortName ? "A" : "Artificial";
			case Columns::Number:			return "Number";
			case Columns::Channel:			return "Channel";
			case Columns::Value:			return "Value";
			case Columns::TransposeAmount:	return "Transpose";
			case Columns::FadeTime:			return "Fade Time";
			case Columns::CoarseDetune:		return "Coarse Detune";
			case Columns::FineDetune:		return "Fine Detune";
			case Columns::Timestamp:		return "Timestamp";
			case Columns::EventId:			return "Event ID";
			default: return "";
			}
		}

		void drawColumnHeader(Graphics& g, Columns c, Rectangle<float> area)
        {
            g.setFont(GLOBAL_BOLD_FONT());
            g.setColour(Colours::black.withAlpha(0.15f));
            g.fillRect(area.reduced(0.5f));
            g.setColour(Colours::white);
			g.drawText(getColumnName(c, true), area, Justification::centred);
        }
        
        void paint(Graphics& g) override
        {
			if (!filterResult.wasOk())
			{
				g.setColour(Colours::red.withSaturation(0.5f));
				g.setFont(GLOBAL_MONOSPACE_FONT());
				g.drawText(filterResult.getErrorMessage(), getLocalBounds().toFloat(), Justification::centred);
			}

            auto b = getLocalBounds();
            auto top = b.removeFromTop(TopHeight).removeFromBottom(HeaderHeight);

            for(int i = 0; i < int(Columns::numColumns); i++)
            {
                auto h = top.removeFromLeft(getColumnWidth((Columns)i));

				if (h.isEmpty())
					continue;

                drawColumnHeader(g, (Columns)i, h.toFloat());
            }
            
			int numActiveNotes = 0;

            for(auto e: events)
            {
                auto a = b.removeFromTop(RowHeight);
				auto copy = a;

				

				if (a.getHeight() < RowHeight)
					break;

                for(int i = 0; i < int(Columns::numColumns); i++)
                {
                    auto h = a.removeFromLeft(getColumnWidth((Columns)i));

					if (h.isEmpty())
						continue;

                    drawEventColumn(g, e, (Columns)i, h.toFloat());
                }

				if (e.isNoteOn())
					numActiveNotes++;
				if (e.isNoteOff())
					numActiveNotes = jmax(0, numActiveNotes - 1);

				if (e.isNoteOn() && numActiveNotes < 4)
				{
					auto c = copy.removeFromLeft(getColumnWidth(Columns::Type)).toFloat();

					UnblurryGraphics ug(g, *this);

					for (int i = 0; i < events.size(); i++)
					{
						if (events[i].isNoteOff() && events[i].getEventId() == e.getEventId())
						{
							c = c.withBottom(TopHeight + (i + 1) * RowHeight).reduced(numActiveNotes*2.0f, RowHeight / 2);


							g.setColour(getColourForEvent(e.getEventId()).withAlpha(1.0f));

							c = c.withRight(c.getCentreX());

							ug.draw1PxHorizontalLine(c.getY(), c.getX(), c.getRight());
							ug.draw1PxHorizontalLine(c.getBottom(), c.getX(), c.getRight());
							ug.draw1PxVerticalLine(c.getX(), c.getY(), c.getBottom());
						}
					}
				}
            }
        }
        
		void resized() override
		{
			rebuildEventsToShow();
			auto topRow = getLocalBounds().removeFromTop(TopHeight);
			topRow.removeFromBottom(HeaderHeight);

			processButton.setBounds(topRow.removeFromLeft(topRow.getHeight()).reduced(1));
			clearButton.setBounds(topRow.removeFromRight(topRow.getHeight()).reduced(1));
			filter.setBounds(topRow);

			resizer.setBounds(getLocalBounds().removeFromRight(15).removeFromBottom(15));
		}
        
        void timerCallback() override
        {
            if(logger != nullptr)
            {
				auto didSomething = !logger->inputQueue.isEmpty() || !logger->outputQueue.isEmpty();
				
				logger->inputQueue.callForEveryElementInQueue([&](const HiseEvent& e)
                {
                    allInputEvents.add(e);
                    return true;
                });
                
				logger->outputQueue.callForEveryElementInQueue([&](const HiseEvent& e)
				{
					allOutputEvents.add(e);
					return true;
				});

				if (allInputEvents.size() > 2048)
					allInputEvents.removeRange(0, 1024);

				if (allOutputEvents.size() > 2048)
					allOutputEvents.removeRange(0, 1024);

				if(didSomething)
					rebuildEventsToShow();
            }
        }
		
		void rebuildEventsToShow()
		{
			int numToDisplay = roundToInt(std::floor((float)(getHeight() - TopHeight) / (float)RowHeight));

			events.clear();

			auto& arrayToUse = processButton.getToggleState() ? allOutputEvents : allInputEvents;

			for (int i = arrayToUse.size() - 1; i >= 0; i--)
			{
				data->setProperty("number", arrayToUse[i].getNoteNumber());
				data->setProperty("velocity", arrayToUse[i].getVelocity());
				data->setProperty("type", (int)arrayToUse[i].getType());
				data->setProperty("channel", arrayToUse[i].getChannel());
				data->setProperty("event_id", arrayToUse[i].getEventId());
				data->setProperty("timestamp", (int)arrayToUse[i].getTimeStamp());
				data->setProperty("artificial", arrayToUse[i].isArtificial());
				data->setProperty("ignored", arrayToUse[i].isIgnored());
				data->setProperty("timer", arrayToUse[i].isTimerEvent());

				if(filterExpression.isEmpty() || engine->evaluate(filterExpression, &filterResult))
				events.insert(0, arrayToUse[i]);

				if (events.size() == numToDisplay)
					break;
			}

			repaint();
		}

		void textEditorReturnKeyPressed(TextEditor&) override
		{
			filterExpression = filter.getText();
			rebuildEventsToShow();
			
		}

	private:

		Colour getColourForEvent(int eventId) const
		{
			return colours[eventId % 32];
		}

		snex::ui::Graph::Icons f;

		ScopedPointer<juce::JavascriptEngine> engine;
		DynamicObject::Ptr data;
		String filterExpression;
		Result filterResult;
		Array<HiseEvent> allInputEvents;
		Array<HiseEvent> allOutputEvents;
		Array<HiseEvent> events;

        WeakReference<EventLogger> logger;
        juce::ResizableCornerComponent resizer;
        WeakReference<MidiProcessor> mp;

		TextEditor filter;

		HiseShapeButton processButton;
		HiseShapeButton clearButton;

		Colour colours[32];
    };
    
    hise::LockfreeQueue<HiseEvent> inputQueue;
	hise::LockfreeQueue<HiseEvent> outputQueue;
    
    JUCE_DECLARE_WEAK_REFERENCEABLE(EventLogger);
};
#endif

void MidiProcessor::logIfEnabled(const HiseEvent& e, bool beforeProcessing)
{
#if USE_BACKEND
    
    SimpleReadWriteLock::ScopedReadLock sl(eventLock);
    
    if(eventLogger != nullptr)
    {
		if(beforeProcessing)
			eventLogger->inputQueue.push(e);
		else
			eventLogger->outputQueue.push(e);
    }
    
#endif
}

void MidiProcessor::setEnableEventLogger(bool shouldBeEnabled)
{
#if USE_BACKEND
    SimpleReadWriteLock::ScopedWriteLock sl(eventLock);
    
    bool isLoggingEvents = eventLogger != nullptr;
    
    if(isLoggingEvents != shouldBeEnabled)
    {
        if(!shouldBeEnabled)
        {
            eventLogger = nullptr;
        }
        else
        {
            eventLogger = new EventLogger();
        }
    }
#endif
}


Component* MidiProcessor::createEventLogComponent()
{
#if USE_BACKEND
    setEnableEventLogger(true);
    return new EventLogger::Display(this, eventLogger);
#else
	return nullptr;
#endif
}

bool MidiProcessor::setArtificialTimestamp(uint16 eventId, int newTimestamp)
{
	return ownerSynth->midiProcessorChain->setArtificialTimestamp(eventId, newTimestamp);
}

void MidiProcessor::addHiseEventToBuffer(const HiseEvent &m)
{
	ownerSynth->midiProcessorChain->addArtificialEvent(m);

	
}

ProcessorEditorBody *MidiProcessor::createEditor(ProcessorEditor *parentEditor)
{
#if USE_BACKEND

	return new EmptyProcessorEditorBody(parentEditor);

#else 

	ignoreUnused(parentEditor);
	jassertfalse;
	return nullptr;

#endif
};

ProcessorEditorBody *MidiProcessorChain::createEditor(ProcessorEditor *parentEditor)
{
#if USE_BACKEND

	return new EmptyProcessorEditorBody(parentEditor);

#else 

	ignoreUnused(parentEditor);
	jassertfalse;
	return nullptr;

#endif
};



void MidiProcessorChain::addArtificialEvent(const HiseEvent& m)
{
	artificialEvents.addEvent(m);
}

bool MidiProcessorChain::setArtificialTimestamp(uint16 eventId, int newTimestamp)
{
	for (auto& e : artificialEvents)
	{
		if (e.getEventId() == eventId)
		{
			e.setTimeStamp(newTimestamp);
			return true;
		}
	}

	for (auto& e : futureEventBuffer)
	{
		if (e.getEventId() == eventId)
		{
			e.setTimeStamp(newTimestamp);
			return true;
		}
	}

	return false;
}

void MidiProcessorChain::renderNextHiseEventBuffer(HiseEventBuffer &buffer, int numSamples)
{
	if (allNotesOffAtNextBuffer)
	{
		buffer.clear();
		buffer.addEvent(HiseEvent(HiseEvent::Type::AllNotesOff, 0, 0, 1));
		allNotesOffAtNextBuffer = false;
	}

	if (!wholeBufferProcessors.isEmpty())
	{
		for (auto wmp : wholeBufferProcessors)
		{
			wmp->preprocessBuffer(buffer, numSamples);
			buffer.alignEventsToRaster<HISE_EVENT_RASTER>(numSamples);
		}
	}

	if (buffer.isEmpty() && futureEventBuffer.isEmpty() && artificialEvents.isEmpty()) return;
    
	logEvents(buffer, true);

	HiseEventBuffer::Iterator it(buffer);
	
	jassert(buffer.timeStampsAreSorted());

	while (HiseEvent* e = it.getNextEventPointer(true, false))
		processHiseEvent(*e);

	buffer.sortTimestamps();
	artificialEvents.sortTimestamps();

	jassert(buffer.timeStampsAreSorted());

	artificialEvents.moveEventsBelow(buffer, numSamples);
	buffer.moveEventsAbove(artificialEvents, numSamples);
	artificialEvents.subtractFromTimeStamps(numSamples);

	logEvents(buffer, false);
}

void MidiProcessorChain::logEvents(HiseEventBuffer& buffer, bool isBefore)
{
#if USE_BACKEND
	HiseEventBuffer::Iterator it(buffer);

	while (auto n = it.getNextEventPointer())
	{
		logIfEnabled(*n, isBefore);

		for (auto p : processors)
			p->logIfEnabled(*n, isBefore);
	}
#endif
}

MidiProcessorFactoryType::MidiProcessorFactoryType(Processor *p) :
		FactoryType(p),
		hardcodedScripts(new HardcodedScriptFactoryType(p))
{
	ADD_NAME_TO_TYPELIST(JavascriptMidiProcessor);
	ADD_NAME_TO_TYPELIST(Transposer);
	ADD_NAME_TO_TYPELIST(MidiPlayer);

	typeNames.addArray(hardcodedScripts->getAllowedTypes());
};

int MidiProcessorFactoryType::fillPopupMenu(PopupMenu &m, int startIndex)
{
	Array<ProcessorEntry> types = getAllowedTypes();

	int index = startIndex;

	for(int i = 0; i < numMidiProcessors; i++)
	{
		m.addItem(i+startIndex, types[i].name);

		index++;

	}

	PopupMenu hardcodedScriptMenu;

	index = hardcodedScripts->fillPopupMenu(hardcodedScriptMenu, numMidiProcessors + startIndex);

	m.addSubMenu("Hardcoded Scripts", hardcodedScriptMenu);

	return index;
}


Processor *MidiProcessorFactoryType::createProcessor(int typeIndex, const String &id) 
{
	MainController *m = getOwnerProcessor()->getMainController();

	MidiProcessor *mp = nullptr;

	if(typeIndex >= numMidiProcessors)
	{
		mp = dynamic_cast<MidiProcessor*>(hardcodedScripts->createProcessor(typeIndex, id));
	}
	else
	{
		auto ms = dynamic_cast<ModulatorSynth*>(getOwnerProcessor());

		switch(typeIndex)
		{
			case scriptProcessor:		mp = new JavascriptMidiProcessor(m, id); break;
			case transposer:			mp = new Transposer(m, id); break;
			case midiFilePlayer:		mp = new MidiPlayer(m, id, ms); break;
			default:					jassertfalse; return nullptr;
		}

		mp->setOwnerSynth(ms);
	}

	return mp;
};

MidiProcessorChain::MidiProcessorChain(MainController *mc, const String &id, Processor *ownerProcessor):
		MidiProcessor(mc, id),
		parentProcessor(ownerProcessor),
		midiProcessorFactory(new MidiProcessorFactoryType(ownerProcessor)),
		allNotesOffAtNextBuffer(false),
		handler(this)
{
	setOwnerSynth(dynamic_cast<ModulatorSynth*>(ownerProcessor));

	setFactoryType(new MidiProcessorFactoryType(ownerProcessor));

	setEditorState(Processor::Visible, false, dontSendNotification);
};



bool MidiProcessorFactoryType::allowType(const Identifier &typeName) const
{
	if (! FactoryType::allowType(typeName) ) return false;

	return true;

}

void MidiProcessorChain::MidiProcessorChainHandler::add(Processor *newProcessor, Processor *siblingToInsertBefore)
{
	{
		MidiProcessor *m = dynamic_cast<MidiProcessor*>(newProcessor);

		jassert(m != nullptr);

		const int index = siblingToInsertBefore == nullptr ? -1 : chain->processors.indexOf(dynamic_cast<MidiProcessor*>(siblingToInsertBefore));

		newProcessor->prepareToPlay(chain->getSampleRate(), chain->getLargestBlockSize());
		newProcessor->setParentProcessor(chain);

		{
			LOCK_PROCESSING_CHAIN(chain);

			newProcessor->setIsOnAir(chain->isOnAir());
			chain->processors.insert(index, m);

			if (m->isProcessingWholeBuffer())
				chain->addWholeBufferProcessor(m);
		}

		if (JavascriptMidiProcessor* sp = dynamic_cast<JavascriptMidiProcessor*>(newProcessor))
		{
			sp->compileScript();
		}
	}
	
	notifyListeners(Listener::ProcessorAdded, newProcessor);
}

} // namespace hise
