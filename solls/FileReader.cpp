#include <solls/FileReader.h>
#include <liblangutil/Exceptions.h>
#include <libsolutil/Exceptions.h>
#include <libsolutil/CommonIO.h>

using solidity::frontend::ReadCallback;
using std::string;

namespace solidity {

// TODO: move to <solidity/interface/ReadFile.h>?
ReadCallback::Result FileReader::readFile(string const& _kind, string const& _path)
{
	try
	{
		printf("FileReader.readFile: %s, %s\n", _kind.c_str(), _path.c_str());

		if (_kind != ReadCallback::kindString(ReadCallback::Kind::ReadFile))
			BOOST_THROW_EXCEPTION(langutil::InternalCompilerError() << util::errinfo_comment(
				"ReadFile callback used as callback kind " +
				_kind
			));
		string validPath = _path;
		if (validPath.find("file://") == 0)
			validPath.erase(0, 7);

		auto const path = m_basePath / validPath;
		auto canonicalPath = boost::filesystem::weakly_canonical(path);
		bool isAllowed = false;
		for (auto const& allowedDir: m_allowedDirectories)
		{
			// If dir is a prefix of boostPath, we are fine.
			if (
				std::distance(allowedDir.begin(), allowedDir.end()) <= std::distance(canonicalPath.begin(), canonicalPath.end()) &&
				std::equal(allowedDir.begin(), allowedDir.end(), canonicalPath.begin())
			)
			{
				isAllowed = true;
				break;
			}
		}
		if (!isAllowed)
			return ReadCallback::Result{false, "File outside of allowed directories."};

		if (!boost::filesystem::exists(canonicalPath))
			return ReadCallback::Result{false, "File not found."};

		if (!boost::filesystem::is_regular_file(canonicalPath))
			return ReadCallback::Result{false, "Not a valid file."};

		// NOTE: we ignore the FileNotFound exception as we manually check above
		auto contents = util::readFileAsString(canonicalPath.string());
		m_sourceCodes[path.generic_string()] = contents;
		printf("FileReader.readFile: result path: %s\n", path.generic_string().c_str());
		return ReadCallback::Result{true, contents};
	}
	catch (util::Exception const& _exception)
	{
		return ReadCallback::Result{false, "Exception in read callback: " + boost::diagnostic_information(_exception)};
	}
	catch (...)
	{
		return ReadCallback::Result{false, "Unknown exception in read callback."};
	}
}

}
