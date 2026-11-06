#include "AlbumManager.h"
#include <iostream>
#include "Constants.h"
#include "MyException.h"
#include "AlbumNotOpenException.h"
#include <algorithm>
#include <stdexcept>
#include <sys/stat.h>

AlbumManager* AlbumManager::s_instance = nullptr;

AlbumManager::AlbumManager(IDataAccess& dataAccess) :
	m_dataAccess(dataAccess), m_nextPictureId(100), m_nextUserId(200)
{
	m_dataAccess.open();

	for (const auto& album : m_dataAccess.getAlbums()) {
		for (const auto& picture : album.getPictures()) {
			m_nextPictureId = std::max(m_nextPictureId, picture.getId());
		}
	}

	s_instance = this;
	SetConsoleCtrlHandler(ctrlHandler, TRUE);
}

BOOL WINAPI AlbumManager::ctrlHandler(DWORD dwCtrlType)
{
	if (dwCtrlType == CTRL_C_EVENT) {
		if (s_instance && s_instance->m_hChildProcess != INVALID_HANDLE_VALUE) {
			TerminateProcess(s_instance->m_hChildProcess, 0);
			CloseHandle(s_instance->m_hChildProcess);
			s_instance->m_hChildProcess = INVALID_HANDLE_VALUE;
			std::cout << "\nExternal application terminated." << std::endl;
			return TRUE;
		}
	}
	return FALSE; // not handled, default behavior kills the gallery
}

void AlbumManager::executeCommand(CommandType command) {
	AlbumManager::handler_func_t handler;
	try {
		handler = m_commands.at(command);
	}
	catch (const std::out_of_range&) {
		throw MyException("Error: Invalid command[" + std::to_string(command) + "]\n");
	}
	// call outside try so other out of range errors are not hidden
	(this->*handler)();
}

void AlbumManager::printHelp() const
{
	std::cout << "Supported Album commands:" << std::endl;
	std::cout << "*************************" << std::endl;

	for (const struct CommandGroup& group : m_prompts) {
		std::cout << group.title << std::endl;
		std::string space(".  ");
		for (const struct CommandPrompt& command : group.commands) {
			space = command.type < 10 ? ".   " : ".  ";
			std::cout << command.type << space << command.prompt << std::endl;
		}
		std::cout << std::endl;
	}
}


// album
void AlbumManager::createAlbum()
{
	std::string userIdStr = getInputFromConsole("Enter user id: ");
	int userId = std::stoi(userIdStr);
	if (!m_dataAccess.doesUserExists(userId)) {
		throw MyException("Error: Can't create album since there is no user with id [" + userIdStr + "]\n");
	}

	std::string name = getInputFromConsole("Enter album name - ");
	if (m_dataAccess.doesAlbumExists(name, userId)) {
		throw MyException("Error: Failed to create album, album with the same name already exists\n");
	}

	Album newAlbum(userId, name);
	m_dataAccess.createAlbum(newAlbum);

	std::cout << "Album [" << newAlbum.getName() << "] created successfully by user@" << newAlbum.getOwnerId() << std::endl;
}

void AlbumManager::openAlbum()
{
	if (isCurrentAlbumSet()) {
		closeAlbum();
	}

	std::string userIdStr = getInputFromConsole("Enter user id: ");
	int userId = std::stoi(userIdStr);
	if (!m_dataAccess.doesUserExists(userId)) {
		throw MyException("Error: Can't open album since there is no user with id @" + userIdStr + ".\n");
	}

	std::string name = getInputFromConsole("Enter album name - ");
	if (!m_dataAccess.doesAlbumExists(name, userId)) {
		throw MyException("Error: Failed to open album, since there is no album with name:" + name + ".\n");
	}

	m_openAlbum = m_dataAccess.openAlbum(name);
	m_currentAlbumName = name;
	std::cout << "Album [" << name << "] opened successfully." << std::endl;
}

void AlbumManager::closeAlbum()
{
	refreshOpenAlbum();

	std::cout << "Album [" << m_openAlbum.getName() << "] closed successfully." << std::endl;
	m_dataAccess.closeAlbum(m_openAlbum);
	m_currentAlbumName = "";
}

void AlbumManager::deleteAlbum()
{
	std::string userIdStr = getInputFromConsole("Enter user id: ");
	int userId = std::stoi(userIdStr);
	if (!m_dataAccess.doesUserExists(userId)) {
		throw MyException("Error: There is no user with id @" + userIdStr + "\n");
	}

	std::string albumName = getInputFromConsole("Enter album name - ");
	if (!m_dataAccess.doesAlbumExists(albumName, userId)) {
		throw MyException("Error: Failed to delete album, since there is no album with name:" + albumName + ".\n");
	}

	if ((isCurrentAlbumSet()) &&
		(m_openAlbum.getOwnerId() == userId && m_openAlbum.getName() == albumName)) {
		closeAlbum();
	}

	m_dataAccess.deleteAlbum(albumName, userId);
	std::cout << "Album [" << albumName << "] @" << userId << " deleted successfully." << std::endl;
}

void AlbumManager::listAlbums()
{
	m_dataAccess.printAlbums();
}

void AlbumManager::listAlbumsOfUser()
{
	std::string userIdStr = getInputFromConsole("Enter user id: ");
	int userId = std::stoi(userIdStr);
	if (!m_dataAccess.doesUserExists(userId)) {
		throw MyException("Error: There is no user with id @" + userIdStr + "\n");
	}

	const User& user = m_dataAccess.getUser(userId);
	const std::list<Album>& albums = m_dataAccess.getAlbumsOfUser(user);

	std::cout << "Albums list of user@" << user.getId() << ":" << std::endl;
	std::cout << "-----------------------" << std::endl;

	for (const auto& album : albums) {
		std::cout << "   + [" << album.getName() << "] - created on " << album.getCreationDate() << std::endl;
	}
}


// picture
void AlbumManager::addPictureToAlbum()
{
	refreshOpenAlbum();

	std::string picName = getInputFromConsole("Enter picture name: ");
	if (m_openAlbum.doesPictureExists(picName)) {
		throw MyException("Error: Failed to add picture, picture with the same name already exists.\n");
	}

	Picture picture(++m_nextPictureId, picName);
	std::string picPath = getInputFromConsole("Enter picture path: ");
	picture.setPath(picPath);

	m_dataAccess.addPictureToAlbumByName(m_openAlbum.getName(), picture);

	std::cout << "Picture [" << picture.getId() << "] successfully added to Album [" << m_openAlbum.getName() << "]." << std::endl;
}

void AlbumManager::removePictureFromAlbum()
{
	refreshOpenAlbum();

	std::string picName = getInputFromConsole("Enter picture name: ");
	if (!m_openAlbum.doesPictureExists(picName)) {
		throw MyException("Error: There is no picture with name <" + picName + ">.\n");
	}

	auto picture = m_openAlbum.getPicture(picName);
	m_dataAccess.removePictureFromAlbumByName(m_openAlbum.getName(), picture.getName());
	std::cout << "Picture <" << picName << "> successfully removed from Album [" << m_openAlbum.getName() << "]." << std::endl;
}

void AlbumManager::listPicturesInAlbum()
{
	refreshOpenAlbum();

	std::cout << "List of pictures in Album [" << m_openAlbum.getName()
		<< "] of user@" << m_openAlbum.getOwnerId() << ":" << std::endl;

	const std::list<Picture>& albumPictures = m_openAlbum.getPictures();
	for (auto iter = albumPictures.begin(); iter != albumPictures.end(); ++iter) {
		std::cout << "   + Picture [" << iter->getId() << "] - " << iter->getName() <<
			"\tLocation: [" << iter->getPath() << "]\tCreation Date: [" <<
			iter->getCreationDate() << "]\tTags: [" << iter->getTagsCount() << "]" << std::endl;
	}
	std::cout << std::endl;
}

void AlbumManager::showPicture()
{
	refreshOpenAlbum();

	std::string picName = getInputFromConsole("Enter picture name: ");
	if (!m_openAlbum.doesPictureExists(picName)) {
		throw MyException("Error: There is no picture with name <" + picName + ">.\n");
	}

	auto pic = m_openAlbum.getPicture(picName);
	if (!fileExistsOnDisk(pic.getPath())) {
		throw MyException("Error: Can't open <" + picName + "> since it doesnt exist on disk.\n");
	}
	system(pic.getPath().c_str());
}

void AlbumManager::openPicture()
{
	refreshOpenAlbum();

	std::string picName = getInputFromConsole("Enter picture name: ");
	if (!m_openAlbum.doesPictureExists(picName)) {
		throw MyException("Error: There is no picture with name <" + picName + ">.\n");
	}

	auto pic = m_openAlbum.getPicture(picName);
	if (!fileExistsOnDisk(pic.getPath())) {
		throw MyException("Error: Can't open <" + picName + "> since it doesn't exist on disk.\n");
	}

	std::cout << "Select software to open with:" << std::endl;
	std::cout << "  1. Paint (mspaint)" << std::endl;
	std::cout << "  2. IrfanView" << std::endl;
	std::string choice = getInputFromConsole("Enter choice (1 or 2): ");

	std::string appPath;
	if (choice == "1") {
		appPath = "mspaint.exe";
	}
	else if (choice == "2") {
		appPath = "C:\\Program Files\\IrfanView\\i_view64.exe";
	}
	else {
		throw MyException("Error: Invalid software choice.\n");
	}

	// "<appPath>" "<picturePath>"
	std::string cmdLine = "\"" + appPath + "\" \"" + pic.getPath() + "\"";

	STARTUPINFOA si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};

	if (!CreateProcessA(
		nullptr,                          // no module name taken from cmdLine
		const_cast<char*>(cmdLine.c_str()),
		nullptr,                          // default process security
		nullptr,                          // default thread security
		FALSE,                            // dont inherit handles
		0,                                // no creation flags
		nullptr,                          // inherit parents environment
		nullptr,                          // inherit parents working directory
		&si,
		&pi))
	{
		throw MyException("Error: Failed to launch application. WinAPI error code: " +
			std::to_string(GetLastError()) + "\n");
	}

	// store child process handle so ctrl + c handler can kill it
	m_hChildProcess = pi.hProcess;
	CloseHandle(pi.hThread); // thread handle not needed

	std::cout << "Opened <" << picName << "> with "
		<< (choice == "1" ? "Paint" : "IrfanView") << "." << std::endl;
	std::cout << "Waiting for application to close... (Press CTRL+C to force-close it)" << std::endl;

	WaitForSingleObject(pi.hProcess, INFINITE);

	// application closed normally, clean up
	CloseHandle(pi.hProcess);
	m_hChildProcess = INVALID_HANDLE_VALUE;
	std::cout << "Application closed." << std::endl;
}

void AlbumManager::tagUserInPicture()
{
	refreshOpenAlbum();

	std::string picName = getInputFromConsole("Enter picture name: ");
	if (!m_openAlbum.doesPictureExists(picName)) {
		throw MyException("Error: There is no picture with name <" + picName + ">.\n");
	}

	Picture pic = m_openAlbum.getPicture(picName);

	std::string userIdStr = getInputFromConsole("Enter user id to tag: ");
	int userId = std::stoi(userIdStr);
	if (!m_dataAccess.doesUserExists(userId)) {
		throw MyException("Error: There is no user with id @" + userIdStr + "\n");
	}
	User user = m_dataAccess.getUser(userId);

	m_dataAccess.tagUserInPicture(m_openAlbum.getName(), pic.getName(), user.getId());
	std::cout << "User @" << userIdStr << " successfully tagged in picture <" << pic.getName() << "> in album [" << m_openAlbum.getName() << "]" << std::endl;
}

void AlbumManager::untagUserInPicture()
{
	refreshOpenAlbum();

	std::string picName = getInputFromConsole("Enter picture name: ");
	if (!m_openAlbum.doesPictureExists(picName)) {
		throw MyException("Error: There is no picture with name <" + picName + ">.\n");
	}

	Picture pic = m_openAlbum.getPicture(picName);

	std::string userIdStr = getInputFromConsole("Enter user id: ");
	int userId = stoi(userIdStr);
	if (!m_dataAccess.doesUserExists(userId)) {
		throw MyException("Error: There is no user with id @" + userIdStr + "\n");
	}
	User user = m_dataAccess.getUser(userId);

	if (!pic.isUserTagged(user)) {
		throw MyException("Error: The user was not tagged! \n");
	}

	m_dataAccess.untagUserInPicture(m_openAlbum.getName(), pic.getName(), user.getId());
	std::cout << "User @" << userIdStr << " successfully untagged in picture <" << pic.getName() << "> in album [" << m_openAlbum.getName() << "]" << std::endl;
}

void AlbumManager::listUserTags()
{
	refreshOpenAlbum();

	std::string picName = getInputFromConsole("Enter picture name: ");
	if (!m_openAlbum.doesPictureExists(picName)) {
		throw MyException("Error: There is no picture with name <" + picName + ">.\n");
	}
	auto pic = m_openAlbum.getPicture(picName);

	const std::set<int> users = pic.getUserTags();

	if (0 == users.size()) {
		throw MyException("Error: There is no user tegged in <" + picName + ">.\n");
	}

	std::cout << "Tagged users in picture <" << picName << ">:" << std::endl;
	for (const int user_id : users) {
		const User user = m_dataAccess.getUser(user_id);
		std::cout << user << std::endl;
	}
	std::cout << std::endl;
}


// user related
void AlbumManager::addUser()
{
	std::string name = getInputFromConsole("Enter user name: ");

	User user(++m_nextUserId, name);

	m_dataAccess.createUser(user);
	std::cout << "User " << name << " with id @" << user.getId() << " created successfully." << std::endl;
}

void AlbumManager::removeUser()
{
	std::string userIdStr = getInputFromConsole("Enter user id to remove: ");
	int userId = std::stoi(userIdStr);

	if (!m_dataAccess.doesUserExists(userId)) {
		throw MyException("Error: There is no user with id @" + userIdStr + "\n");
	}

	User user = m_dataAccess.getUser(userId);
	m_dataAccess.deleteUser(user);

	std::cout << "User @" << userId << " removed successfully." << std::endl;
}

void AlbumManager::listUsers()
{
	m_dataAccess.printUsers();
}

void AlbumManager::userStatistics()
{
	std::string userIdStr = getInputFromConsole("Enter user id: ");
	int userId = std::stoi(userIdStr);
	if (!m_dataAccess.doesUserExists(userId)) {
		throw MyException("Error: There is no user with id @" + userIdStr + "\n");
	}

	const User& user = m_dataAccess.getUser(userId);

	std::cout << "user @" << userId << " Statistics:" << std::endl << "--------------------" << std::endl <<
		"  + Count of Albums Tagged: " << m_dataAccess.countAlbumsTaggedOfUser(user) << std::endl <<
		"  + Count of Tags: " << m_dataAccess.countTagsOfUser(user) << std::endl <<
		"  + Avarage Tags per Alboum: " << m_dataAccess.averageTagsPerAlbumOfUser(user) << std::endl <<
		"  + Count of Albums Owned: " << m_dataAccess.countAlbumsOwnedOfUser(user) << std::endl;
}


// datavase queries
void AlbumManager::topTaggedUser()
{
	const User& user = m_dataAccess.getTopTaggedUser();
	std::cout << "The top tagged user is: " << user.getName() << std::endl;
}

void AlbumManager::topTaggedPicture()
{
	const Picture& picture = m_dataAccess.getTopTaggedPicture();
	std::cout << "The top tagged picture is: " << picture.getName() << std::endl;
}

void AlbumManager::picturesTaggedUser()
{
	std::string userIdStr = getInputFromConsole("Enter user id: ");
	int userId = std::stoi(userIdStr);
	if (!m_dataAccess.doesUserExists(userId)) {
		throw MyException("Error: There is no user with id @" + userIdStr + "\n");
	}

	auto user = m_dataAccess.getUser(userId);
	auto taggedPictures = m_dataAccess.getTaggedPicturesOfUser(user);

	std::cout << "List of pictures that User@" << user.getId() << " tagged :" << std::endl;
	for (const Picture& picture : taggedPictures) {
		std::cout << "   + " << picture << std::endl;
	}
	std::cout << std::endl;
}


// help and exit
void AlbumManager::exit()
{
	std::exit(EXIT_SUCCESS);
}

void AlbumManager::help()
{
	system("CLS");
	printHelp();
}

std::string AlbumManager::getInputFromConsole(const std::string& message)
{
	std::string input;
	do {
		std::cout << message;
		std::getline(std::cin, input);
	} while (input.empty());

	return input;
}

bool AlbumManager::fileExistsOnDisk(const std::string& filename)
{
	struct stat buffer;
	return (stat(filename.c_str(), &buffer) == 0);
}

void AlbumManager::refreshOpenAlbum() {
	if (!isCurrentAlbumSet()) {
		throw AlbumNotOpenException();
	}
	m_openAlbum = m_dataAccess.openAlbum(m_currentAlbumName);
}

bool AlbumManager::isCurrentAlbumSet() const
{
	return !m_currentAlbumName.empty();
}

const std::vector<struct CommandGroup> AlbumManager::m_prompts = {
	{
		"Supported Albums Operations:\n----------------------------",
		{
			{ CREATE_ALBUM        , "Create album" },
			{ OPEN_ALBUM          , "Open album" },
			{ CLOSE_ALBUM         , "Close album" },
			{ DELETE_ALBUM        , "Delete album" },
			{ LIST_ALBUMS         , "List albums" },
			{ LIST_ALBUMS_OF_USER , "List albums of user" }
		}
	},
	{
		"Supported Album commands (when specific album is open):",
		{
			{ ADD_PICTURE    , "Add picture." },
			{ REMOVE_PICTURE , "Remove picture." },
			{ SHOW_PICTURE   , "Show picture." },
			{ LIST_PICTURES  , "List pictures." },
			{ TAG_USER		 , "Tag user." },
			{ UNTAG_USER	 , "Untag user." },
			{ LIST_TAGS		 , "List tags." },
			{ OPEN_PICTURE   , "Open picture with external app." }
		}
	},
	{
		"Supported Users commands: ",
		{
			{ ADD_USER         , "Add user." },
			{ REMOVE_USER      , "Remove user." },
			{ LIST_OF_USER     , "List of users." },
			{ USER_STATISTICS  , "User statistics." },
		}
	},
	{
		"Supported Queries:",
		{
			{ TOP_TAGGED_USER      , "Top tagged user." },
			{ TOP_TAGGED_PICTURE   , "Top tagged picture." },
			{ PICTURES_TAGGED_USER , "Pictures tagged user." },
		}
	},
	{
		"Supported Operations:",
		{
			{ HELP , "Help (clean screen)" },
			{ EXIT , "Exit." },
		}
	}
};

const std::map<CommandType, AlbumManager::handler_func_t> AlbumManager::m_commands = {
	{ CREATE_ALBUM,        &AlbumManager::createAlbum },
	{ OPEN_ALBUM,          &AlbumManager::openAlbum },
	{ CLOSE_ALBUM,         &AlbumManager::closeAlbum },
	{ DELETE_ALBUM,        &AlbumManager::deleteAlbum },
	{ LIST_ALBUMS,         &AlbumManager::listAlbums },
	{ LIST_ALBUMS_OF_USER, &AlbumManager::listAlbumsOfUser },
	{ ADD_PICTURE,         &AlbumManager::addPictureToAlbum },
	{ REMOVE_PICTURE,      &AlbumManager::removePictureFromAlbum },
	{ LIST_PICTURES,       &AlbumManager::listPicturesInAlbum },
	{ SHOW_PICTURE,        &AlbumManager::showPicture },
	{ OPEN_PICTURE,        &AlbumManager::openPicture },
	{ TAG_USER,            &AlbumManager::tagUserInPicture },
	{ UNTAG_USER,          &AlbumManager::untagUserInPicture },
	{ LIST_TAGS,           &AlbumManager::listUserTags },
	{ ADD_USER,            &AlbumManager::addUser },
	{ REMOVE_USER,         &AlbumManager::removeUser },
	{ LIST_OF_USER,        &AlbumManager::listUsers },
	{ USER_STATISTICS,     &AlbumManager::userStatistics },
	{ TOP_TAGGED_USER,     &AlbumManager::topTaggedUser },
	{ TOP_TAGGED_PICTURE,  &AlbumManager::topTaggedPicture },
	{ PICTURES_TAGGED_USER,&AlbumManager::picturesTaggedUser },
	{ HELP,                &AlbumManager::help },
	{ EXIT,                &AlbumManager::exit }
};