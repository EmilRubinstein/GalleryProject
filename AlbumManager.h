#pragma once
#include <vector>
#include "Constants.h"
#include "MemoryAccess.h"
#include "Album.h"
// stop windows max macro breaking std max
#define NOMINMAX
#include <windows.h>
#include <map>

class AlbumManager
{
public:
	AlbumManager(IDataAccess& dataAccess);

	void executeCommand(CommandType command);
	void printHelp() const;

	using handler_func_t = void (AlbumManager::*)(void);

private:
	HANDLE m_hChildProcess{ INVALID_HANDLE_VALUE };// to keep track of the opened picture process
	int m_nextPictureId{};
	int m_nextUserId{};
	std::string m_currentAlbumName{};
	IDataAccess& m_dataAccess;
	Album m_openAlbum;
	static BOOL WINAPI ctrlHandler(DWORD dwCtrlType);
	static AlbumManager* s_instance;

	void help();
	// albums management
	void createAlbum();
	void openAlbum();
	void closeAlbum();
	void deleteAlbum();
	void listAlbums();
	void listAlbumsOfUser();

	// Picture management
	void addPictureToAlbum();
	void removePictureFromAlbum();
	void listPicturesInAlbum();
	void showPicture();
	void openPicture();

	// tags related
	void tagUserInPicture();
	void untagUserInPicture();
	void listUserTags();

	// users management
	void addUser();
	void removeUser();
	void listUsers();
	void userStatistics();

	void topTaggedUser();
	void topTaggedPicture();
	void picturesTaggedUser();
	void exit();

	std::string getInputFromConsole(const std::string& message);
	bool fileExistsOnDisk(const std::string& filename);
	void refreshOpenAlbum();
	bool isCurrentAlbumSet() const;

	static const std::vector<struct CommandGroup> m_prompts;
	static const std::map<CommandType, handler_func_t> m_commands;

};