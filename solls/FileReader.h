#pragma once

#include <boost/filesystem.hpp>
#include <map>
#include <vector>

#include <libsolidity/interface/ReadFile.h>

namespace solidity {

// XXX this could be merged with <libsolidity/interface/ReadFile.h> as and be reused in CommandLineInCommandLineInterface.
class FileReader
{
public: // XXX private
	boost::filesystem::path m_basePath;
	std::vector<boost::filesystem::path> m_allowedDirectories;
	std::map<std::string, std::string> m_sourceCodes;
	std::map<std::string /*input path*/, std::string /*full path*/> m_fullPathMapping;

public:
	FileReader(
		boost::filesystem::path _basePath,
		std::vector<boost::filesystem::path> _allowedDirectories
	):
		m_basePath(std::move(_basePath)),
		m_allowedDirectories(std::move(_allowedDirectories)),
		m_sourceCodes()
	{}

	boost::filesystem::path& basePath() noexcept { return m_basePath; }
	boost::filesystem::path const& basePath() const noexcept { return m_basePath; }

	std::vector<boost::filesystem::path>& allowedDirectories() noexcept { return m_allowedDirectories; }
	std::vector<boost::filesystem::path> const& allowedDirectories() const noexcept { return m_allowedDirectories; }

	std::map<std::string /*input path*/, std::string /*full path*/> const& fullPathMapping() const noexcept { return m_fullPathMapping; }

	frontend::ReadCallback::Result readFile(std::string const& _kind, std::string const& _path);
};

}
