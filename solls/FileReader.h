#pragma once

#include <boost/filesystem.hpp>
#include <map>
#include <vector>

#include <libsolidity/interface/ReadFile.h>

namespace solidity {

class FileReader
{
public: // XXX private
	boost::filesystem::path m_basePath;
	std::vector<boost::filesystem::path> m_allowedDirectories;
	std::map<std::string, std::string> m_sourceCodes;

public:
	frontend::ReadCallback::Result readFile(std::string const& _kind, std::string const& _path);
};

}
