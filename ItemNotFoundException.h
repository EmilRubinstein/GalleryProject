#pragma once
#include <string>
#include "MyException.h"

class ItemNotFoundException : public MyException {
public:
	// message built once so what stays valid
	ItemNotFoundException(const std::string& item, int id) :
		MyException(item + " with id " + std::to_string(id) + " does not exist") {}
	ItemNotFoundException(const std::string& item, const std::string& name) :
		MyException(item + " with name " + name + " does not exist") {}
};