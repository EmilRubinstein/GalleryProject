#include "Album.h"
#include "ItemNotFoundException.h"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>


Album::Album(int ownerId, const std::string& name) :
	m_ownerId(ownerId), m_name(name), m_pictures{}
{
	setCreationDateNow();
}

Album::Album(int ownerId, const std::string& name, const std::string& creationTime) :
	m_ownerId(ownerId), m_name(name), m_creationDate(creationTime), m_pictures{}
{
	// Left empty
}


const std::string& Album::getName() const
{
	return m_name;
}

void Album::setName(const std::string& name)
{
	m_name = name;
}

int Album::getOwnerId() const
{
	return m_ownerId;
}

void Album::setOwner(int userId)
{
	m_ownerId = userId;
}

std::string Album::getCreationDate() const
{
	return m_creationDate;
}

void Album::setCreationDate(const std::string& creationTime)
{
	m_creationDate = creationTime;
}

void Album::setCreationDateNow()
{
	time_t now = time(nullptr);
	std::stringstream oss;
	oss << std::put_time(localtime(&now), "%d/%m/%Y %H:%M:%S");
	m_creationDate = oss.str();
}


Picture Album::getPicture(const std::string& pictureName) const
{
	for (auto& picture : m_pictures) {
		if (pictureName == picture.getName()) {
			return picture;
		}
	}
	throw ItemNotFoundException("Picture", pictureName);
}


const std::list<Picture>& Album::getPictures() const
{
	return m_pictures;
}

void Album::untagUserInAlbum(int userId)
{
	for (auto& picture : m_pictures) {
		picture.untagUser(userId);
	}
}

void Album::tagUserInAlbum(int userId)
{
	for (auto& picture : m_pictures) {
		picture.tagUser(userId);
	}
}

void Album::untagUserInPicture(int userId, const std::string& pictureName)
{
	for (auto& picture : m_pictures) {
		if (picture.getName() == pictureName) {
			picture.untagUser(userId);
		}
	}
}

void Album::tagUserInPicture(int userId, const std::string& pictureName)
{
	for (auto& picture : m_pictures) {
		if (picture.getName() == pictureName) {
			picture.tagUser(userId);
		}
	}
}

void Album::addPicture(const Picture& picture)
{
	m_pictures.push_back(picture);
}


void Album::removePicture(const std::string& pictureName)
{
	auto it = std::find_if(m_pictures.begin(), m_pictures.end(), [&](const Picture& picture) {
		return picture.getName() == pictureName;
		});

	if (it == m_pictures.end()) {
		throw ItemNotFoundException("Picture", pictureName);
	}
	m_pictures.erase(it);
}


bool Album::doesPictureExists(const std::string& name) const
{
	for (const auto& picture : m_pictures) {
		if (name == picture.getName()) {
			return true;
		}
	}
	return false;
}

bool Album::operator==(const Album& other) const
{
	return m_ownerId == other.getOwnerId();
}



std::ostream& operator<<(std::ostream& strOut, const Album& album)
{
	strOut << "[" << album.m_name << "] - created on " << album.m_creationDate << std::endl;
	return strOut;
}