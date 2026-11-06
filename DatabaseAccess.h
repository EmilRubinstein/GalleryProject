#pragma once
#include "IDataAccess.h"
#include "sqlite3.h"
#include <string>

class DatabaseAccess : public IDataAccess
{
public:
	explicit DatabaseAccess(const std::string& dbPath = "gallery.db");
	~DatabaseAccess() override;

	DatabaseAccess(const DatabaseAccess&) = delete;
	DatabaseAccess& operator=(const DatabaseAccess&) = delete;

	// album related
	const std::list<Album> getAlbums() override;
	const std::list<Album> getAlbumsOfUser(const User& user) override;
	void createAlbum(const Album& album) override;
	void deleteAlbum(const std::string& albumName, int userId) override;
	bool doesAlbumExists(const std::string& albumName, int userId) override;
	Album openAlbum(const std::string& albumName) override;
	void closeAlbum(Album& pAlbum) override;
	void printAlbums() override;

	// picture related
	void addPictureToAlbumByName(const std::string& albumName, const Picture& picture) override;
	void removePictureFromAlbumByName(const std::string& albumName, const std::string& pictureName) override;
	void tagUserInPicture(const std::string& albumName, const std::string& pictureName, int userId) override;
	void untagUserInPicture(const std::string& albumName, const std::string& pictureName, int userId) override;

	// user related
	void printUsers() override;
	User getUser(int userId) override;
	void createUser(User& user) override;
	void deleteUser(const User& user) override;
	bool doesUserExists(int userId) override;

	// user statistics
	int countAlbumsOwnedOfUser(const User& user) override;
	int countAlbumsTaggedOfUser(const User& user) override;
	int countTagsOfUser(const User& user) override;
	float averageTagsPerAlbumOfUser(const User& user) override;

	// queries
	User getTopTaggedUser() override;
	Picture getTopTaggedPicture() override;
	std::list<Picture> getTaggedPicturesOfUser(const User& user) override;

	bool open() override;
	void close() override;
	void clear() override;

private:
	sqlite3* m_db{ nullptr };
	std::string m_dbPath;

	void execute(const std::string& sql);
	int getAlbumId(const std::string& albumName);
	int getPictureId(int albumId, const std::string& pictureName);
	std::list<Album> fetchAlbums(const User* owner);
	void loadPictures(int albumId, Album& album);
	void insertPicture(int albumId, const Picture& picture);
};