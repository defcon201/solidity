#pragma once

#include <string>

namespace lsp {

/// Logging facility within the scope of a Language Server.
///
/// Since language servers are typically automatically spawned by their clients,
/// logging cannot just go to stderr.
///
/// This API allows proper abstracting logging into the client's log channel.
class Logger
{
public:
	virtual ~Logger() = default;

	enum class MessageType {
		Error = 1,
		Warning = 2,
		Info = 3,
		Log = 4,
	};

	virtual void log(MessageType _type, std::string const& _message) = 0;

	// Convenience helper methods for logging in various severities.
	//
	void logError(std::string const& _msg) { log(MessageType::Error, _msg); }
	void logWarning(std::string const& _msg) { log(MessageType::Warning, _msg); }
	void logInfo(std::string const& _msg) { log(MessageType::Info, _msg); }
	void logMessage(std::string const& _msg) { log(MessageType::Log, _msg); }
};

} // end namespace
