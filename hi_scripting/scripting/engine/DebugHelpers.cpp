

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
*   which also must be licenced for commercial applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

namespace hise { using namespace juce;

String getArrayTextForVar(const var& value)
{
	if (auto ar = value.getArray())
	{
		auto& arrayToStringify = *ar;
		String output;
		output << "[";
		const int maxSize = jmin<int>(4, arrayToStringify.size());

		for (int i = 0; i < maxSize - 1; i++)
		{
			output << getArrayTextForVar(arrayToStringify[i]) << ", ";
		}

		output << getArrayTextForVar(arrayToStringify[maxSize - 1]);

		if (maxSize != arrayToStringify.size())
			output << ", (...)]";
		else
			output << "]";

		return output;
	}
    else if(auto debugObject = dynamic_cast<DebugableObject*>(value.getObject()))
    {
        return debugObject->getDebugName();
    }
    
    return value.toString();
}
    
String DebugInformation::varArrayToString(const Array<var> &arrayToStringify)
{
	var ar(arrayToStringify);
	return getArrayTextForVar(ar);
}

struct BufferViewer : public Component,
					  public ApiProviderBase::ApiComponentBase,
					  public Timer
{
	BufferViewer(DebugInformation* info, ApiProviderBase::Holder* holder_) :
		ApiComponentBase(holder_),
		Component("Buffer Viewer")
	{
		setFromDebugInformation(info);
		addAndMakeVisible(thumbnail);
		thumbnail.setShouldScaleVertically(true);
		startTimer(500);
		setSize(500, 200);
	}

    void providerCleared() override
    {
        bufferToUse = nullptr;
        
    }
    
	void providerWasRebuilt() override
	{
		if (auto p = getProviderBase())
		{
			for (int i = 0; i < p->getNumDebugObjects(); i++)
			{
				auto di = p->getDebugInformation(i);

				if (di->getCodeToInsert() == codeToInsert)
				{
					setFromDebugInformation(dynamic_cast<DebugInformation*>(di.get()));
					dirty = true;
					return;
				}
			};
		}
	};

	void setFromDebugInformation(DebugInformation* info)
	{
		if (info != nullptr)
		{
			codeToInsert = info->getCodeToInsert();
			bufferToUse = info->getVariantCopy().getBuffer();
		}
	}
	
	void timerCallback() override
	{
		if (dirty && bufferToUse != nullptr)
		{
			thumbnail.setBuffer(var(bufferToUse.get()));
			dirty = false;
		}
	}

	void resized() override
	{
		thumbnail.setBounds(getLocalBounds());
	}

	bool dirty = true;

	HiseAudioThumbnail thumbnail;
	String codeToInsert;
	WeakReference<VariantBuffer> bufferToUse;
};


Component* DebugInformation::createPopupComponent(const MouseEvent& e, Component* componentToNotify)
{
	if (auto c = DebugInformationBase::createPopupComponent(e, componentToNotify))
	{
		return c;
	}

	var v = getVariantCopy();

	if (v.isBuffer())
	{
#if USE_BACKEND

		PanelWithProcessorConnection* pc = componentToNotify->findParentComponentOfClass<PanelWithProcessorConnection>();

		if (pc == nullptr)
		{
			auto co = dynamic_cast<ControlledObject*>(componentToNotify);

			if (co == nullptr)
				co = componentToNotify->findParentComponentOfClass<ControlledObject>();

			if (co != nullptr)
			{
				if (auto activeEditor = co->getMainController()->getLastActiveEditor())
				{
					pc = activeEditor->findParentComponentOfClass<PanelWithProcessorConnection>();
				}
			}
		}

		if (pc != nullptr)
		{
			auto p = pc->getProcessor();
			auto holder = dynamic_cast<ApiProviderBase::Holder*>(p);
			jassert(holder != nullptr);

			auto display = new BufferViewer(this, holder);
			return display;
		}

#endif
		return nullptr;
	}

	if (v.isObject() || v.isArray())
	{
		return DebugableObject::Helpers::createJSONEditorForObject(e, componentToNotify, v, getTextForName());
	}

	return nullptr;
}

void DebugInformation::doubleClickCallback(const MouseEvent &e, Component* componentToNotify)
{
	auto obj = getObject();

	if (auto pc = componentToNotify->findParentComponentOfClass<PanelWithProcessorConnection>())
	{
		auto p = pc->getConnectedProcessor();
		DebugableObject::Helpers::gotoLocation(p, this);
		return;
	}

	

	if (auto cso = dynamic_cast<ScriptingObject*>(obj))
	{
		auto jp = dynamic_cast<Processor*>(cso->getProcessor());

		if (auto sc = dynamic_cast<ScriptingApi::Content::ScriptComponent*>(getObject()))
		{
			auto b = jp->getMainController()->getScriptComponentEditBroadcaster();
			b->setSelection(sc, sendNotification);
		}

		DebugableObject::Helpers::gotoLocation(jp, this);
	}
}

String DebugInformation::getTextForRow(Row r)
{
	switch (r)
	{
	case Row::Name: return getTextForName();
	case Row::Type: return getTextForType();
	case Row::DataType: return getTextForDataType();
	case Row::Value: return getTextForValue();
    case Row::numRows:   return "";
	}

	return "";
}

String DebugInformation::toString()
{
	String output;

	output << "Name: " << getTextForRow(Row::Name) << ", ";
	output << "Type: " << getTextForRow(Row::Type) << ", ";
	output << "DataType:" << getTextForRow(Row::DataType) << ", ";
	output << "Value: " << getTextForRow(Row::Value);

	return output;
}

#if USE_BACKEND
CodeEditorPanel* findOrCreateEditorPanel(CodeEditorPanel* panel, Processor* processor, DebugableObject::Location location)
{
	auto getSanitizedId = [](DebugableObject::Location l)
	{
		auto s = l.fileName;

		if (s.isEmpty())
			return String("onInit");

		if (s.contains("("))
			return s.removeCharacters("()");
			
		if (File::isAbsolutePath(s))
			return File(s).getFileName();

		return s;
	};

	auto matches = [&](CodeEditorPanel* p)
	{
		if (p->getConnectedProcessor() == processor)
		{
			StringArray indexList;
			p->fillIndexList(indexList);
			auto idx = p->getCurrentIndex();
			auto id = indexList[idx];

			auto expId = getSanitizedId(location);

			if (expId == id)
				return true;
		}

		return false;
	};
	
	if (matches(panel))
		return panel;

	if (auto tabs = panel->getParentShell()->findParentComponentOfClass<FloatingTabComponent>())
	{
		int idx = 0;
		if (location.fileName.isNotEmpty())
		{
			StringArray indexList;
			panel->fillIndexList(indexList);

			auto expId = getSanitizedId(location);
			idx = indexList.indexOf(expId);
		}

		return CodeEditorPanel::showOrCreateTab(tabs, dynamic_cast<JavascriptProcessor*>(processor), idx);
	}

	return panel;
}


struct UndoableLocationSwitch: public UndoableAction
{
    static String getLocationString(JavascriptProcessor* p, const String& indexString)
    {
        if(indexString == "onInit")
            return "";
        
        for(int i = 0; i < p->getNumWatchedFiles(); i++)
        {
            auto f = p->getWatchedFile(i);
            
            if(f.getFileName() == indexString)
            {
                return f.getFullPathName();
            }
        }
        
        return indexString + "()";
    }
    
    static String getDescription(Processor* p)
    {
        String d;
        
        if (auto editor = p->getMainController()->getLastActiveEditor())
        {
            if (auto editorPanel = editor->findParentComponentOfClass<CodeEditorPanel>())
            {
                mcl::TextEditor& e = dynamic_cast<mcl::FullEditor*>(editor)->editor;
                auto s = e.getTextDocument().getSelection(0).head;
                
                StringArray indexList;
                editorPanel->fillIndexList(indexList);
                auto idx = editorPanel->getCurrentIndex();
                d << indexList[idx];
                d << ":";
                d << String(s.x);
                
            }
        }
        
        
        
        return d;
    }
    
    DebugableObject::Location getPosition(Processor* p)
    {
        DebugableObject::Location location;
        
        if (auto editor = p->getMainController()->getLastActiveEditor())
        {
            if (auto editorPanel = editor->findParentComponentOfClass<CodeEditorPanel>())
            {
                mcl::TextEditor& e = dynamic_cast<mcl::FullEditor*>(editor)->editor;
                auto s = e.getTextDocument().getSelection(0).head;
                
                CodeDocument::Position pos(e.getDocument(), s.x, s.y);
                
                StringArray indexList;
                editorPanel->fillIndexList(indexList);
                auto idx = editorPanel->getCurrentIndex();
                
                location.charNumber = pos.getPosition();
                location.fileName = getLocationString(dynamic_cast<JavascriptProcessor*>(p), indexList[idx]);
            }
        }
        
        return location;
    }
    
    UndoableLocationSwitch(Processor* p, DebugableObject::Location location)
    {
        newProcessor = p;
        newLocation = location;
        
        if (auto editor = p->getMainController()->getLastActiveEditor())
        {
            if (auto editorPanel = editor->findParentComponentOfClass<CodeEditorPanel>())
                oldProcessor = editorPanel->getConnectedProcessor();
        }
        
        oldLocation = getPosition(oldProcessor);
    }
    
    bool perform() override
    {
        if(oldProcessor != nullptr)
            oldLocation = getPosition(oldProcessor);
        
        return gotoInternal(newProcessor.get(), newLocation);
    }
    
    bool undo() override
    {
        if(newProcessor != nullptr)
            newLocation = getPosition(newProcessor);
        
        return gotoInternal(oldProcessor.get(), oldLocation);
    }
    
    bool gotoInternal(Processor* processor, DebugableObject::Location location)
    {
        if(processor == nullptr)
            return false;
        
        auto editor = processor->getMainController()->getLastActiveEditor();

        if (editor == nullptr)
            return false;

        if (auto editorPanel = editor->findParentComponentOfClass<CodeEditorPanel>())
        {
            editorPanel = findOrCreateEditorPanel(editorPanel, processor, location);
            editorPanel->gotoLocation(processor, location.fileName, location.charNumber);
            return true;
        }
        else if (location.fileName.isNotEmpty())
        {
            auto jsp = dynamic_cast<JavascriptProcessor*>(processor);

            File f(location.fileName);

            jsp->showPopupForFile(f, location.charNumber);
            return true;
        }
        else if (auto scriptEditor = editor->findParentComponentOfClass<ScriptingEditor>())
        {
            scriptEditor->showOnInitCallback();
            scriptEditor->gotoChar(location.charNumber);
            return true;
        }
    
        return false;
    }
    
    WeakReference<Processor> oldProcessor, newProcessor;
    DebugableObject::Location oldLocation, newLocation;
};
#endif

void gotoLocationInternal(Processor* processor, DebugableObject::Location location)
{
#if USE_BACKEND
    auto um = processor->getMainController()->getLocationUndoManager();
    
    um->beginNewTransaction();
    um->perform(new UndoableLocationSwitch(processor, location),
                UndoableLocationSwitch::getDescription(processor));
    
    processor->getMainController()->getCommandManager()->commandStatusChanged();
    
#else
    ignoreUnused(processor, location);
#endif
}

void DebugableObject::Helpers::gotoLocation(Component* ed, JavascriptProcessor* sp, const Location& location)
{
#if USE_BACKEND
	auto handler = dynamic_cast<ScriptEditHandler*>(ed);

	if (sp == nullptr && handler != nullptr)
	{
		sp = handler->getScriptEditHandlerProcessor();
	}

	if (sp == nullptr)
	{
		// You have to somehow manage to pass the processor here...
		jassertfalse;
		return;
	}

	gotoLocationInternal(dynamic_cast<Processor*>(sp), location);

#else
	ignoreUnused(ed, sp, location);
#endif
}


void DebugableObject::Helpers::gotoLocation(Processor* processor, DebugInformationBase* info)
{
	gotoLocationInternal(processor, info->getLocation());
}



DebugableObject::Location DebugableObject::Helpers::getLocationFromProvider(Processor* p, DebugableObjectBase* obj)
{
	auto loc = obj->getLocation();

	if (loc.charNumber != 0 || loc.fileName.isNotEmpty())
		return loc;

	if (auto asProvider = dynamic_cast<ApiProviderBase::Holder*>(p))
	{
		auto engine = asProvider->getProviderBase();

		if (auto ptr = getDebugInformation(engine, obj))
			return ptr->getLocation();
	}

	return loc;
}

Component* DebugableObject::Helpers::showProcessorEditorPopup(const MouseEvent& e, Component* table, Processor* p)
{
#if USE_BACKEND
	if (p != nullptr)
	{
		ProcessorEditorContainer *pc = new ProcessorEditorContainer();
		pc->setRootProcessorEditor(p);
        pc->setName(p->getId());
		return pc;
	}
	else
	{
		PresetHandler::showMessageWindow("Processor does not exist", "The Processor is not existing, because it was deleted or the reference is wrong", PresetHandler::IconType::Error);
        return nullptr;
	}
#else
	ignoreUnused(e, table, p);
	return nullptr;
#endif
}

Component* DebugableObject::Helpers::createJSONEditorForObject(const MouseEvent& e, Component* table, var object, const String& id)
{
	auto cleanedObject = getCleanedObjectForJSONDisplay(object);

	JSONEditor* jsonEditor = new JSONEditor(cleanedObject);

	jsonEditor->setName((cleanedObject.isArray() ? "Show Array: " : "Show Object: ") + id);
	jsonEditor->setSize(500, 500);
	
	return jsonEditor;
}

void DebugableObject::Helpers::showJSONEditorForObject(const MouseEvent& e, Component* table, var object, const String& id)
{
#if USE_BACKEND
	auto jsonEditor = createJSONEditorForObject(e, table, object, id);
	auto e2 = e.getEventRelativeTo(table);
	GET_BACKEND_ROOT_WINDOW(table)->getRootFloatingTile()->showComponentInRootPopup(jsonEditor, table, Point<int>(table->getWidth() / 2, e2.getMouseDownY() + 5));
#else
	ignoreUnused(e, table, object, id);
#endif
}


var DebugableObject::Helpers::getCleanedObjectForJSONDisplay(const var& object)
{
	if (object.isBuffer())
	{
		return var(object.getBuffer()->toDebugString());
	}
	else if (auto obj = object.getDynamicObject())
	{
		var copy = var(new DynamicObject());

		auto source = obj->getProperties();
		auto& destination = copy.getDynamicObject()->getProperties();

		for (int i = 0; i < source.size(); i++)
		{
			destination.set(source.getName(i), getCleanedObjectForJSONDisplay(source.getValueAt(i)));
		}

		return copy;
	}
	else if (auto ar = object.getArray())
	{
		auto& source = *ar;

		Array<var> destination;

		for (const auto& v : source)
		{
			destination.add(getCleanedObjectForJSONDisplay(v));
		}

		return var(destination);
	}
	else if (auto debugObject = dynamic_cast<DebugableObjectBase*>(object.getObject()))
	{
		String valueText;
		valueText << debugObject->getDebugName() << ": " << debugObject->getDebugValue();

		return var(valueText);
	}
	else
		return object;
}

DebugInformationBase::Ptr DebugableObject::Helpers::getDebugInformation(ApiProviderBase* engine, DebugableObjectBase* object)
{
	for (int i = 0; i < engine->getNumDebugObjects(); i++)
	{
		auto dobj = engine->getDebugInformation(i);

		if (auto ptr = getDebugInformation(dobj, object))
			return ptr;
	}

	return nullptr;
}

DebugInformationBase::Ptr DebugableObject::Helpers::getDebugInformation(ApiProviderBase* engine, const var& v)
{
	if (auto obj = dynamic_cast<DebugableObjectBase*>(v.getObject()))
	{
		return getDebugInformation(engine, obj);
	}

	for (int i = 0; i < engine->getNumDebugObjects(); i++)
	{
		auto b = engine->getDebugInformation(i);

		if(auto dbg = dynamic_cast<DebugInformation*>(b.get()))
		{
			if(dbg->getVariantCopy() == v)
				return b;
		}
	}

	return nullptr;
}

hise::DebugInformationBase::Ptr DebugableObject::Helpers::getDebugInformation(DebugInformationBase::Ptr parent, DebugableObjectBase* object)
{
	if (parent->getObject() == object)
		return parent;

	for (int i = 0; i < parent->getNumChildElements(); i++)
	{
		if (auto c = parent->getChildElement(i))
		{
			if (auto p = getDebugInformation(c, object))
				return p;
		}
	}

	return nullptr;
}

} // namespace hise
