#include "DatabaseAccess.h"
#include "MyException.h"
#include "ItemNotFoundException.h"
#include <iostream>
#include <vector>

namespace
{
	class Statement
	{
	public:
		Statement(sqlite3* db, const std::string& sql) : m_db(db)
		{
			if (sqlite3_prepare_v2(db, sql.c_str(), -1, &m_stmt, nullptr) != SQLITE_OK) {
				throw MyException(std::string("Database error: ") + sqlite3_errmsg(db));
			}
		}

		~Statement()
		{
			sqlite3_finalize(m_stmt);
		}

		Statement(const Statement&) = delete;
		Statement& operator=(const Statement&) = delete;

		Statement& bind(int index, int value)
		{
			sqlite3_bind_int(m_stmt, index, value);
			return *this;
		}

		Statement& bind(int index, const std::string& value)
		{
			sqlite3_bind_text(m_stmt, index, value.c_str(), -1, SQLITE_TRANSIENT);
			return *this;
		}

		bool step()
		{
			int result = sqlite3_step(m_stmt);
			if (result == SQLITE_ROW) {
				return true;
			}
			if (result == SQLITE_DONE) {
				return false;
			}
			throw MyException(std::string("Database error: ") + sqlite3_errmsg(m_db));
		}

		int intColumn(int column) const
		{
			return sqlite3_column_int(m_stmt, column);
		}

		std::string textColumn(int column) const
		{
			const unsigned char* text = sqlite3_column_text(m_stmt, column);
			return text ? reinterpret_cast<const char*>(text) : "";
		}

	private:
		sqlite3* m_db;
		sqlite3_stmt* m_stmt{ nullptr };
	};

	void loadTags(sqlite3* db, Picture& picture)
	{
		Statement st(db, "SELECT USER_ID FROM TAGS WHERE PICTURE_ID = ?;");
		st.bind(1, picture.getId());
		while (st.step()) {
			picture.tagUser(st.intColumn(0));
		}
	}

	std::list<Picture> readPictures(sqlite3* db, Statement& st)
	{
		std::list<Picture> pictures;
		while (st.step()) {
			pictures.emplace_back(st.intColumn(0), st.textColumn(1), st.textColumn(2), st.textColumn(3));
		}
		for (Picture& picture : pictures) {
			loadTags(db, picture);
		}
		return pictures;
	}

	const char* const SCHEMA =
		"CREATE TABLE IF NOT EXISTS USERS ("
		"  ID INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,"
		"  NAME TEXT NOT NULL);"
		"CREATE TABLE IF NOT EXISTS ALBUMS ("
		"  ID INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,"
		"  NAME TEXT NOT NULL,"
		"  CREATION_DATE TEXT NOT NULL,"
		"  USER_ID INTEGER NOT NULL,"
		"  UNIQUE (NAME, USER_ID));"
		"CREATE TABLE IF NOT EXISTS PICTURES ("
		"  ID INTEGER PRIMARY KEY NOT NULL,"
		"  NAME TEXT NOT NULL,"
		"  LOCATION TEXT NOT NULL,"
		"  CREATION_DATE TEXT NOT NULL,"
		"  ALBUM_ID INTEGER NOT NULL,"
		"  UNIQUE (NAME, ALBUM_ID),"
		"  FOREIGN KEY (ALBUM_ID) REFERENCES ALBUMS (ID) ON DELETE CASCADE);"
		"CREATE TABLE IF NOT EXISTS TAGS ("
		"  ID INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,"
		"  PICTURE_ID INTEGER NOT NULL,"
		"  USER_ID INTEGER NOT NULL,"
		"  UNIQUE (PICTURE_ID, USER_ID),"
		"  FOREIGN KEY (PICTURE_ID) REFERENCES PICTURES (ID) ON DELETE CASCADE,"
		"  FOREIGN KEY (USER_ID) REFERENCES USERS (ID) ON DELETE CASCADE);";
}


DatabaseAccess::DatabaseAccess(const std::string& dbPath) :
	m_dbPath(dbPath)
{}

DatabaseAccess::~DatabaseAccess()
{
	close();
}

bool DatabaseAccess::open()
{
	if (m_db) {
		return true;
	}

	if (sqlite3_open(m_dbPath.c_str(), &m_db) != SQLITE_OK) {
		std::string message = sqlite3_errmsg(m_db);
		sqlite3_close(m_db);
		m_db = nullptr;
		throw MyException("Database error: " + message);
	}

	execute("PRAGMA foreign_keys = ON;");
	execute(SCHEMA);
	return true;
}

void DatabaseAccess::close()
{
	if (m_db) {
		sqlite3_close(m_db);
		m_db = nullptr;
	}
}

void DatabaseAccess::clear()
{
	execute("DELETE FROM TAGS; DELETE FROM PICTURES; DELETE FROM ALBUMS; DELETE FROM USERS;");
}

void DatabaseAccess::execute(const std::string& sql)
{
	char* error = nullptr;
	if (sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
		std::string message = error ? error : "unknown error";
		sqlite3_free(error);
		throw MyException("Database error: " + message);
	}
}

int DatabaseAccess::getAlbumId(const std::string& albumName)
{
	Statement st(m_db, "SELECT ID FROM ALBUMS WHERE NAME = ? ORDER BY ID LIMIT 1;");
	st.bind(1, albumName);
	if (!st.step()) {
		throw ItemNotFoundException("Album", albumName);
	}
	return st.intColumn(0);
}

int DatabaseAccess::getPictureId(int albumId, const std::string& pictureName)
{
	Statement st(m_db, "SELECT ID FROM PICTURES WHERE ALBUM_ID = ? AND NAME = ?;");
	st.bind(1, albumId).bind(2, pictureName);
	if (!st.step()) {
		throw ItemNotFoundException("Picture", pictureName);
	}
	return st.intColumn(0);
}

void DatabaseAccess::loadPictures(int albumId, Album& album)
{
	Statement st(m_db, "SELECT ID, NAME, LOCATION, CREATION_DATE FROM PICTURES WHERE ALBUM_ID = ? ORDER BY ID;");
	st.bind(1, albumId);
	for (const Picture& picture : readPictures(m_db, st)) {
		album.addPicture(picture);
	}
}

void DatabaseAccess::insertPicture(int albumId, const Picture& picture)
{
	Statement st(m_db, "INSERT INTO PICTURES (ID, NAME, LOCATION, CREATION_DATE, ALBUM_ID) VALUES (?, ?, ?, ?, ?);");
	st.bind(1, picture.getId())
		.bind(2, picture.getName())
		.bind(3, picture.getPath())
		.bind(4, picture.getCreationDate())
		.bind(5, albumId);
	st.step();

	for (int userId : picture.getUserTags()) {
		Statement tag(m_db, "INSERT OR IGNORE INTO TAGS (PICTURE_ID, USER_ID) VALUES (?, ?);");
		tag.bind(1, picture.getId()).bind(2, userId);
		tag.step();
	}
}

std::list<Album> DatabaseAccess::fetchAlbums(const User* owner)
{
	struct Row
	{
		int id;
		int ownerId;
		std::string name;
		std::string date;
	};

	std::vector<Row> rows;
	{
		Statement st(m_db, owner ?
			"SELECT ID, USER_ID, NAME, CREATION_DATE FROM ALBUMS WHERE USER_ID = ? ORDER BY ID;" :
			"SELECT ID, USER_ID, NAME, CREATION_DATE FROM ALBUMS ORDER BY ID;");
		if (owner) {
			st.bind(1, owner->getId());
		}
		while (st.step()) {
			rows.push_back({ st.intColumn(0), st.intColumn(1), st.textColumn(2), st.textColumn(3) });
		}
	}

	std::list<Album> albums;
	for (const Row& row : rows) {
		Album album(row.ownerId, row.name, row.date);
		loadPictures(row.id, album);
		albums.push_back(album);
	}
	return albums;
}


// ******************* Album *******************
const std::list<Album> DatabaseAccess::getAlbums()
{
	return fetchAlbums(nullptr);
}

const std::list<Album> DatabaseAccess::getAlbumsOfUser(const User& user)
{
	return fetchAlbums(&user);
}

void DatabaseAccess::createAlbum(const Album& album)
{
	{
		Statement st(m_db, "INSERT INTO ALBUMS (NAME, CREATION_DATE, USER_ID) VALUES (?, ?, ?);");
		st.bind(1, album.getName()).bind(2, album.getCreationDate()).bind(3, album.getOwnerId());
		st.step();
	}

	int albumId = static_cast<int>(sqlite3_last_insert_rowid(m_db));
	for (const Picture& picture : album.getPictures()) {
		insertPicture(albumId, picture);
	}
}

void DatabaseAccess::deleteAlbum(const std::string& albumName, int userId)
{
	Statement st(m_db, "DELETE FROM ALBUMS WHERE NAME = ? AND USER_ID = ?;");
	st.bind(1, albumName).bind(2, userId);
	st.step();
}

bool DatabaseAccess::doesAlbumExists(const std::string& albumName, int userId)
{
	Statement st(m_db, "SELECT 1 FROM ALBUMS WHERE NAME = ? AND USER_ID = ?;");
	st.bind(1, albumName).bind(2, userId);
	return st.step();
}

Album DatabaseAccess::openAlbum(const std::string& albumName)
{
	Statement st(m_db, "SELECT ID, USER_ID, NAME, CREATION_DATE FROM ALBUMS WHERE NAME = ? ORDER BY ID LIMIT 1;");
	st.bind(1, albumName);
	if (!st.step()) {
		throw MyException("No album with name " + albumName + " exists");
	}

	Album album(st.intColumn(1), st.textColumn(2), st.textColumn(3));
	loadPictures(st.intColumn(0), album);
	return album;
}

void DatabaseAccess::closeAlbum(Album&)
{}

void DatabaseAccess::printAlbums()
{
	Statement st(m_db, "SELECT USER_ID, NAME, CREATION_DATE FROM ALBUMS ORDER BY ID;");
	if (!st.step()) {
		throw MyException("There are no existing albums.");
	}

	std::cout << "Album list:" << std::endl;
	std::cout << "-----------" << std::endl;
	do {
		Album album(st.intColumn(0), st.textColumn(1), st.textColumn(2));
		std::cout << "* " << album;
	} while (st.step());
}


// ******************* Picture *******************
void DatabaseAccess::addPictureToAlbumByName(const std::string& albumName, const Picture& picture)
{
	insertPicture(getAlbumId(albumName), picture);
}

void DatabaseAccess::removePictureFromAlbumByName(const std::string& albumName, const std::string& pictureName)
{
	int albumId = getAlbumId(albumName);

	Statement st(m_db, "DELETE FROM PICTURES WHERE ALBUM_ID = ? AND NAME = ?;");
	st.bind(1, albumId).bind(2, pictureName);
	st.step();

	if (sqlite3_changes(m_db) == 0) {
		throw ItemNotFoundException("Picture", pictureName);
	}
}

void DatabaseAccess::tagUserInPicture(const std::string& albumName, const std::string& pictureName, int userId)
{
	int pictureId = getPictureId(getAlbumId(albumName), pictureName);

	Statement st(m_db, "INSERT OR IGNORE INTO TAGS (PICTURE_ID, USER_ID) VALUES (?, ?);");
	st.bind(1, pictureId).bind(2, userId);
	st.step();
}

void DatabaseAccess::untagUserInPicture(const std::string& albumName, const std::string& pictureName, int userId)
{
	int pictureId = getPictureId(getAlbumId(albumName), pictureName);

	Statement st(m_db, "DELETE FROM TAGS WHERE PICTURE_ID = ? AND USER_ID = ?;");
	st.bind(1, pictureId).bind(2, userId);
	st.step();
}


// ******************* User *******************
void DatabaseAccess::printUsers()
{
	std::cout << "Users list:" << std::endl;
	std::cout << "-----------" << std::endl;

	Statement st(m_db, "SELECT ID, NAME FROM USERS ORDER BY ID;");
	while (st.step()) {
		std::cout << User(st.intColumn(0), st.textColumn(1)) << std::endl;
	}
}

User DatabaseAccess::getUser(int userId)
{
	Statement st(m_db, "SELECT NAME FROM USERS WHERE ID = ?;");
	st.bind(1, userId);
	if (!st.step()) {
		throw ItemNotFoundException("User", userId);
	}
	return User(userId, st.textColumn(0));
}

void DatabaseAccess::createUser(User& user)
{
	Statement st(m_db, "INSERT INTO USERS (NAME) VALUES (?);");
	st.bind(1, user.getName());
	st.step();
	user.setId(static_cast<int>(sqlite3_last_insert_rowid(m_db)));
}

void DatabaseAccess::deleteUser(const User& user)
{
	Statement st(m_db, "DELETE FROM USERS WHERE ID = ?;");
	st.bind(1, user.getId());
	st.step();

	if (sqlite3_changes(m_db) == 0) {
		throw ItemNotFoundException("User", user.getId());
	}
}

bool DatabaseAccess::doesUserExists(int userId)
{
	Statement st(m_db, "SELECT 1 FROM USERS WHERE ID = ?;");
	st.bind(1, userId);
	return st.step();
}


// ******************* Statistics *******************
int DatabaseAccess::countAlbumsOwnedOfUser(const User& user)
{
	Statement st(m_db, "SELECT COUNT(*) FROM ALBUMS WHERE USER_ID = ?;");
	st.bind(1, user.getId());
	st.step();
	return st.intColumn(0);
}

int DatabaseAccess::countAlbumsTaggedOfUser(const User& user)
{
	Statement st(m_db,
		"SELECT COUNT(DISTINCT P.ALBUM_ID) FROM TAGS T "
		"JOIN PICTURES P ON P.ID = T.PICTURE_ID WHERE T.USER_ID = ?;");
	st.bind(1, user.getId());
	st.step();
	return st.intColumn(0);
}

int DatabaseAccess::countTagsOfUser(const User& user)
{
	Statement st(m_db, "SELECT COUNT(*) FROM TAGS WHERE USER_ID = ?;");
	st.bind(1, user.getId());
	st.step();
	return st.intColumn(0);
}

float DatabaseAccess::averageTagsPerAlbumOfUser(const User& user)
{
	int albumsTaggedCount = countAlbumsTaggedOfUser(user);

	if (0 == albumsTaggedCount) {
		return 0;
	}

	return static_cast<float>(countTagsOfUser(user)) / albumsTaggedCount;
}


// ******************* Queries *******************
User DatabaseAccess::getTopTaggedUser()
{
	Statement st(m_db, "SELECT USER_ID FROM TAGS GROUP BY USER_ID ORDER BY COUNT(*) DESC, USER_ID ASC LIMIT 1;");
	if (!st.step()) {
		throw MyException("There isn't any tagged user.");
	}
	return getUser(st.intColumn(0));
}

Picture DatabaseAccess::getTopTaggedPicture()
{
	Statement st(m_db,
		"SELECT P.ID, P.NAME, P.LOCATION, P.CREATION_DATE FROM PICTURES P "
		"JOIN TAGS T ON T.PICTURE_ID = P.ID GROUP BY P.ID ORDER BY COUNT(*) DESC, P.ID ASC LIMIT 1;");
	std::list<Picture> pictures = readPictures(m_db, st);

	if (pictures.empty()) {
		throw MyException("There isn't any tagged picture.");
	}
	return pictures.front();
}

std::list<Picture> DatabaseAccess::getTaggedPicturesOfUser(const User& user)
{
	Statement st(m_db,
		"SELECT P.ID, P.NAME, P.LOCATION, P.CREATION_DATE FROM PICTURES P "
		"JOIN TAGS T ON T.PICTURE_ID = P.ID WHERE T.USER_ID = ? ORDER BY P.ID;");
	st.bind(1, user.getId());
	return readPictures(m_db, st);
}