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

#ifndef PRESET_HANDLER_H_INCLUDED
#define PRESET_HANDLER_H_INCLUDED

namespace hise { using namespace juce;

#define PRESET_MENU_ITEM_DELTA 80
#define CLIPBOARD_ITEM_MENU_INDEX 999

class MainController;
class Chain;
class Processor;
class FactoryType;
class DeactiveOverlay;

#if USE_BACKEND

#define PRODUCT_ID ""
#define PUBLIC_KEY ""

#endif



class AboutPage : public Component,
				  public ButtonListener
{
public:

    AboutPage();

	void refreshText();

	void buttonClicked(Button *b) override;

	void mouseDown(const MouseEvent &) override;


	void resized() override
	{
		refreshText();

#if USE_BACKEND
		//checkUpdateButton->setBounds(16, getHeight() - 32, 100, 24);
#endif
	}

	void paint(Graphics &g) override;

	void setUserEmail(const String &userEmail_)
	{
		userEmail = userEmail_;

		refreshText();
	}

private:

	AttributedString infoData;

	String userEmail;

	ScopedPointer<TextButton> checkUpdateButton;

    Image aboutHeader;
};

class ModulatorSynthChain;

class PoolCollection;

/** The base class for handling external resources. 
*	@ingroup core
*
*	HISE uses a strict root-folder based encapsulation for every project with
*	dedicated sub folders for each file type.
*	
*	In compiled plugins, these resources will either be embedded into the plugin,
*	shipped as compressed data file along with the binary or use a customizable
*	path for the sample data.
*
*	Regardless whether you use HISE with C++ only or develop your project with
*	the HISE application, this system will be used for resolving external data
*	references.
*
*	*/
class FileHandlerBase: public ControlledObject
{
public:

	/** The sub folders of each project folder. */
	enum SubDirectories
	{
		AudioFiles, ///< all audio files that will not be used by the streaming engine (impulse responses, loops, one-shot samples)
		Images, ///< image resources
		SampleMaps, ///< files containing the mapping information for a particular sample set.
		MidiFiles, ///< MIDI files that are embedded into the project
		UserPresets, ///< restorable UI states
		Samples, ///< audio files that are used by the streaming engine
		Scripts, ///< Javascript files
		Binaries, /// the temporary build folder for the project. NEVER EVER PUT ANY C++ FILE HERE.
		Presets, ///< contains the autosave state of your current project as well as project states saved in binary format (admittedly the most poorly named thing in HISE)
		XMLPresetBackups, ///< the project state in human readable form. This is the preferred file format for HISE projects
		AdditionalSourceCode, ///< the folder for additional source code that will be included in the compilation of the project. All files in this directory will be automatically added to the IDE projects and compiled along the autogenerated C++ files and HISE code. Everything you write in C++ for your project must be put in this directory.
		Documentation, ///< the markdown documentation for your project
		DspNetworks, ///< contains all custom DSP algorithms
		numSubDirectories
	};

	virtual ~FileHandlerBase();

	virtual File getSubDirectory(SubDirectories dir) const;

	static String getIdentifier(SubDirectories dir);

	static SubDirectories getSubDirectoryForIdentifier(Identifier id);

	/** creates a absolute path from the pathToFile and the specified sub directory. */
	String getFilePath(const String &pathToFile, SubDirectories subDir) const;

	/** Creates a reference string that can be used to obtain the file in the project directory.
	*
	*	If the file is not in
	*/

	const String getFileReference(const String &absoluteFileName, SubDirectories dir) const;

	Array<File> getFileList(SubDirectories dir, bool sortByTime = false, bool searchInSubfolders = false) const;

	/** checks if this is a absolute path (including absolute win paths on OSX and absolute OSX paths on windows); */
	static bool isAbsolutePathCrossPlatform(const String &pathName);

	/** This returns the filename for an absolute path independent of the OS.
	    It can be used to convert an absolute path created on another platform to a valid reference string for ID
		purposes (use includeParentDirectory to decrease the probability of collisions).
	*/
	static String getFileNameCrossPlatform(String pathName, bool includeParentDirectory);

	static File getLinkFile(const File &subDirectory);

    static File getFolderOrRedirect(const File& folder);
    
	/** Creates a platform dependant file in the subdirectory that redirects to another location.
	*
	*	This is mainly used for storing audio samples at another location to keep the project folder size small.
	*/
	void createLinkFile(SubDirectories dir, const File &relocation);

	static void createLinkFileInFolder(const File& source, const File& target);

	void createLinkFileToGlobalSampleFolder(const String& suffix);

	virtual ValueTree getEmbeddedNetwork(const String& id) { return {}; }

	virtual File getRootFolder() const = 0;

	virtual Array<SubDirectories> getSubDirectoryIds() const;

	static String getWildcardForFiles(SubDirectories directory);

	void exportAllPoolsToTemporaryDirectory(ModulatorSynthChain* chain, DialogWindowWithBackgroundThread::LogData* logData=nullptr);

	File getTempFolderForPoolResources() const;

	File getTempFileForPool(SubDirectories dir) const;

	static void loadOtherReferencedImages(ModulatorSynthChain* chainToExport);

	ScopedPointer<PoolCollection> pool;

	void checkSubDirectories();

	void checkAllSampleMaps();

	Result updateSampleMapIds(bool silentMode);

protected:

	friend class MainController;
	friend class ExpansionHandler;

	FileHandlerBase(MainController* mc_);;

	struct FolderReference
	{
		SubDirectories directoryType = numSubDirectories;
		bool isReference = false;
		File file;
	};

	

	File checkSubDirectory(SubDirectories dir);


	

	Array<FolderReference> subDirectories;
};


/** This class handles the file management inside HISE.
*
*	It assumes a working directory and supplies correct paths for all OSes relative to the project root folder.
*
*/
class ProjectHandler: public FileHandlerBase
{
public:

	ProjectHandler(MainController* mc_):
		FileHandlerBase(mc_)
	{
		
	}

	struct Listener
	{
		virtual ~Listener() {};

		/** Whenever a project is changed, this method is called on its registered Listeners. */
		virtual void projectChanged(const File& newRootDirectory) = 0;

	private:

		JUCE_DECLARE_WEAK_REFERENCEABLE(Listener);
	};

	void createNewProject(File &workingDirectory, Component* mainEditor);

	Result setWorkingProject(const File &workingDirectory, bool checkDirectories=true);

	static const StringArray &getRecentWorkDirectories() { return recentWorkDirectories; }

	File getRootFolder() const override { return getWorkDirectory(); }

	File getWorkDirectory() const;

	ValueTree getEmbeddedNetwork(const String& id) override;

	/** Checks if a directory is redirected. */

	bool isRedirected(ProjectHandler::SubDirectories dir) const
	{
		return subDirectories[(int)dir].isReference;
	}

	/** Checks if the ProjectHandler is active (if a directory is set). */
	bool isActive() const;
	
	
	/** */
	void setProjectSettings(Component *mainEditor=nullptr);

	/** Fills the given array with the contents of the specified directory. If 'sortByTime' is true, the most recent files will be the first items in the list. */
	void createRSAKey() const;

	String getPublicKey() const;

	String getPrivateKey() const;

	static String getPublicKeyFromFile(const File& f);
	static String getPrivateKeyFromFile(const File& f);

	void checkActiveProject();

	void addListener(Listener* newProjectListener)
	{
		listeners.addIfNotAlreadyThere(newProjectListener);
	}

	void removeListener(Listener* listenerToRemove)
	{
		listeners.removeAllInstancesOf(listenerToRemove);
	}

	static File getAppDataRoot();
   
	static File getAppDataDirectory();
	
	void restoreWorkingProjects();

private:



	

	bool isValidProjectFolder(const File &file) const;

	
	
	

	bool anySubdirectoryExists(const File& possibleProjectFolder) const;

private:

	Array<WeakReference<Listener>, CriticalSection> listeners;

	File currentWorkDirectory;
	
	static StringArray recentWorkDirectories;

	Component::SafePointer<Component> window;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectHandler);
};



/** This class handles the file resources for compiled plugins. 
*/
class FrontendHandler : public FileHandlerBase
{
public:

	FrontendHandler(MainController* mc):
		FileHandlerBase(mc)
	{
		
	}

	/** returns the app data folder. */
	File getRootFolder() const override;

	File getSubDirectory(SubDirectories directory) const override;

	/** Returns the directory where the samples are located. */
	static File getSampleLocationForCompiledPlugin();
    File getEmbeddedResourceDirectory() const;

	static File getLicenseKey();
	static String getLicenseKeyExtension();

	/** Changes the sample location. */
	static void setSampleLocation(const File &newLocation);

	static File getSampleLinkFile();

	/** Returns the location for the user presets. */
	static File getUserPresetDirectory(bool redirect=true);

	/** There is a folder in the app data directory of your plugin that can be used
	*	to store audio files loaded into your plugin with a relative path to be 
	*	compatible across systems. */
	static File getAdditionalAudioFilesDirectory();

	static String getRelativePathForAdditionalAudioFile(const File& audioFile);
	static File getAudioFileForRelativePath(const String& relativePath);

	static String getProjectName();
	static String getCompanyName();
	static String getCompanyWebsiteName();
	static String getCompanyCopyright();
	static String getVersionString();
	static String getAppGroupId();
	static String getExpansionKey();
	static String getExpansionType();

	static String checkSampleReferences(MainController* mc, bool returnTrueIfOneSampleFound);

	/** on IOS this returns the folder where all the resources (samples, images, etc) are found.
	*	It uses a shared folder for both the AUv3 and Standalone version in order to avoid duplicating the data. */
	static File getResourcesFolder();

	static const bool checkSamplesCorrectlyInstalled();

	/** This returns the app data directory, which must be created by the installer of your product.
	*
	*	On OSX this will be ("Users/Library/Application Support/Company/Product/") and on Windows ("Users/AppData/Local/Company/Product").
	*
	*	This directory will be used for:
	*	- sample location folder (using a LinkOS file)
	*	- user presets (in the UserPresets subfolder)
	*	- license key file
	*/
	static File getAppDataDirectory();

	void setValueTree(SubDirectories type, ValueTree tree)
	{
		jassert(type == UserPresets);

		if (type == UserPresets)
			presets = tree;
	}

	ValueTree getValueTree(SubDirectories type) const
	{
		jassert(type == UserPresets);

		if (type == UserPresets)
			return presets;

		return ValueTree();
	}

	ValueTree getEmbeddedNetwork(const String& id) override;

	void setNetworkData(const ValueTree& nData)
	{
		networks = nData;
	}

	ValueTree networks;

	bool shouldLoadSamplesAfterSetup() const { return samplesCorrectlyLoaded; };

	void loadSamplesAfterSetup();

	void setAllSampleReferencesCorrect()
	{
		samplesCorrectlyLoaded = true;
	}

	bool areSamplesLoadedCorrectly() const { return samplesCorrectlyLoaded; }

	bool areSampleReferencesCorrect() const
	{
		return samplesCorrectlyLoaded;
	}

	void checkAllSampleReferences();

private:

#if HISE_IOS
	bool samplesCorrectlyLoaded = true;
#else
	bool samplesCorrectlyLoaded = true;
#endif

	ValueTree presets;

	File root;
};

#if USE_BACKEND
using NativeFileHandler = ProjectHandler;
#else
using NativeFileHandler = FrontendHandler;
#endif


class UserPresetHelpers
{
public:
    
    static void saveUserPreset(ModulatorSynthChain *chain, const String& targetFile=String(), NotificationType notify=sendNotification);
    
	static ValueTree createUserPreset(ModulatorSynthChain* chain);

	static void addRequiredExpansions(const MainController* mc, ValueTree& preset);

	static StringArray checkRequiredExpansions(MainController* mc, ValueTree& preset);

	static ValueTree createModuleStateTree(ModulatorSynthChain* chain);

    static void loadUserPreset(ModulatorSynthChain *chain, const File &fileToLoad);

	static void restoreModuleStates(ModulatorSynthChain* chain, const ValueTree& v);

	static void loadUserPreset(ModulatorSynthChain* chain, const ValueTree &v);
    
	static Identifier getAutomationIndexFromOldVersion(const String& oldVersion, int oldIndex);

	static bool updateVersionNumber(ModulatorSynthChain* chain, const File& fileToUpdate);

	static bool checkVersionNumber(ModulatorSynthChain* chain, XmlElement& element);

	static String getCurrentVersionNumber(ModulatorSynthChain* chain);

	static ValueTree collectAllUserPresets(ModulatorSynthChain* chain, FileHandlerBase* expansion=nullptr);

	static StringArray getExpansionsForUserPreset(const File& userpresetFile);

	static void extractUserPresets(const char* userPresetData, size_t size);


	static void extractPreset(ValueTree preset, File parent);

	static void extractDirectory(ValueTree directory, File parent);


};

/** A helper class which provides loading and saving Processors to files and clipboard. 
*	@ingroup utility
*
*/
class PresetHandler
{
public:

	enum class IconType
	{
		Info = 0,
		Warning,
		Question,
		Error,
		numIconTypes
	};

	/** Saves the Processor into a subfolder of the directory provided with getPresetFolder(). */
	static void saveProcessorAsPreset(Processor *p, const String &directory=String());
	
	static void copyProcessorToClipboard(Processor *p);

	/** Opens a modal window that allow renaming of a Processor. */
	static String getCustomName(const String &typeName, const String& message=String());

	/** Opens a Yes/No box (HI Style) */
	static bool showYesNoWindow(const String &title, const String &message, IconType icon=IconType::Question);

	/** Opens a Yes/No box (HI Style) or uses the defaultReturnValue if the thread is not the message thread. */
	static bool showYesNoWindowIfMessageThread(const String &title, const String &message, bool defaultReturnValue, IconType icon = IconType::Question);

	/** Opens a message box (HI Style) */
	static void showMessageWindow(const String &title, const String &message, IconType icon=IconType::Info);


	/** Checks if an child processor has a already taken name. If silentMode is false, it will display a message box at the end. */
	static void checkProcessorIdsForDuplicates(Processor *rootProcessor, bool silentMode=true);

	/** Returns a popupmenu with all suiting Processors for the supplied FactoryType. */
	static PopupMenu getAllSavedPresets(int minIndex, Processor *parentChain);

	static void stripViewsFromPreset(ValueTree &preset)
	{
		preset.removeProperty("views", nullptr);
		preset.removeProperty("currentView", nullptr);

		preset.removeProperty("EditorState", nullptr);

		for(int i = 0; i < preset.getNumChildren(); i++)
		{
            ValueTree child = preset.getChild(i);
            
			stripViewsFromPreset(child);
		}
	}
    
    static File loadFile(const String &extension)
    {
		jassert(extension.isEmpty() || extension.startsWith("*"));

        FileChooser fc("Load File", File(), extension, true);
        
        if(fc.browseForFileToOpen())
        {
            
            return fc.getResult();
        }
        return File();
    }
	
    static void saveFile(const String &dataToSave, const String &extension)
    {
		jassert(extension.isEmpty() || extension.startsWith("*"));

        FileChooser fc("Save File", File(), extension);
        
        if(fc.browseForFileToSave(true))
        {
            fc.getResult().deleteFile();
            fc.getResult().create();
            fc.getResult().appendText(dataToSave);
        }
        
    }

    static void setChanged(Processor *p);
    
	/** Checks if the. */
	static String getProcessorNameFromClipboard(const FactoryType *t);

	/** Creates a processor from the Popupmenu. 
	*
	*	It will be connected to the MainController after creation.
	*
	*	@param menuIndexDelta - the menu index of the selected popupitem from the PopupMenu received with getAllSavedPresets.
	*							If the menu was added to another menu as submenu, you have to subtract the last item index before the submenu.
	*	@param m		      - the main controller. This must not be a nullptr!
	*
	*	@returns			  - a connected and restored Processor which can be added to a chain.
	*/
	static Processor *createProcessorFromPreset(int menuIndexDelta, Processor *parent);

	static File getPresetFileFromMenu(int menuIndexDelta, Processor *parent);

	/** Creates a processor from xml data in the clipboard.
	*
	*	The XML data must be parsed before this function, but it checks if a Processor can be created from the data.
	*/
	static Processor *createProcessorFromClipBoard(Processor *parent);

	static void setUniqueIdsForProcessor(Processor * root);

	static ValueTree changeFileStructureToNewFormat(const ValueTree &v);

	/** Opens a file dialog and saves the new path into the library's setting file. */
	static File getSampleFolder(const String &libraryName)
	{
		const bool search = NativeMessageBox::showOkCancelBox(AlertWindow::WarningIcon, "Sample Folder can't be found", "The sample folder for " + libraryName + "can't be found. Press OK to search or Cancel to abort loading");

		if(search)
		{
			FileChooser fc("Searching Sample Folder");

			if(fc.browseForDirectory())
			{
				File sampleFolder = fc.getResult();

				

				return sampleFolder;
			}
		}
		

		return File();
		
		
	}
    
	static File getGlobalScriptFolder(Processor* p);

    static AudioFormatReader *getReaderForFile(const File &file);
    
    static AudioFormatReader *getReaderForInputStream(InputStream *stream);

	static void checkMetaParameters(Processor* p);

	static ValueTree loadValueTreeFromData(const void* data, size_t size, bool wasCompressed)
	{
		if (wasCompressed)
		{
			return ValueTree::readFromGZIPData(data, size);
		}
		else
		{
			return ValueTree::readFromData(data, size);
		}
	}

	static void writeValueTreeAsFile(const ValueTree &v, const String &fileName, bool compressData=false)
	{
		File file(fileName);
		file.deleteFile();
		file.create();

		if (compressData)
		{
			FileOutputStream fos(file);

			GZIPCompressorOutputStream gzos(&fos, 9, false);

			MemoryOutputStream mos;

			v.writeToStream(mos);

			gzos.write(mos.getData(), mos.getDataSize());
			gzos.flush();
		}
		else
		{
			FileOutputStream fos(file);

			v.writeToStream(fos);
		}
	}

	static var writeValueTreeToMemoryBlock(const ValueTree &v, bool compressData=false)
	{

        juce::MemoryBlock mb;

		if (compressData)
		{
			MemoryOutputStream mos(mb, false);

			GZIPCompressorOutputStream gzos(&mos, 9, false);

			MemoryOutputStream internalMos;

			v.writeToStream(internalMos);

			gzos.write(internalMos.getData(), internalMos.getDataSize());
			gzos.flush();
		}
		else
		{
			MemoryOutputStream mos(mb, false);

			v.writeToStream(mos);
		}

		return var(mb.getData(), mb.getSize());
	}

	static void writeSampleMapsToValueTree(ValueTree &sampleMapTree, ValueTree &preset);

	static void buildProcessorDataBase(Processor *root);

	static XmlElement *buildFactory(FactoryType *t, const String &factoryName);

	// creates a processor from the file
	static Processor *loadProcessorFromFile(File fileName, Processor *parent);

	static void setCurrentMainController(void* mc)
	{
		currentController = mc;
	}

	static LookAndFeel* createAlertWindowLookAndFeel()
	{
		return HiseColourScheme::createAlertWindowLookAndFeel(currentController);
	}

private:

	static void* currentController;

	

	//static void handlePreset(int menuIndexDelta, Processor *p, bool createNewProcessor)

	// Returns the subdirectory for each processor type
	static File getDirectory(Processor *p);

};

struct MessageWithIcon : public Component
{
	struct LookAndFeelMethods
	{
        virtual ~LookAndFeelMethods() {};
        
		virtual void paintMessage(MessageWithIcon& icon, Graphics& g);

		virtual MarkdownLayout::StyleData getAlertWindowMarkdownStyleData();

		virtual Image createIcon(PresetHandler::IconType type);
	};

	MessageWithIcon(PresetHandler::IconType type, LookAndFeel* laf, const String &message);

	void paint(Graphics &g) override;

	MarkdownRenderer r;

	PresetHandler::IconType t;
	LookAndFeelMethods defaultLaf;
	int bestWidth;
	Image image;
};

} // namespace hise

#endif
