/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0
#pragma once

#include <solls/FileReader.h>

#include <liblsp/Server.h>
#include <liblsp/VFS.h>
#include <liblsp/protocol.h>

#include <libsolidity/interface/CompilerStack.h>
#include <libsolidity/interface/ReadFile.h>

#include <json/value.h>

#include <boost/filesystem/path.hpp>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace solidity {

/// Solidity Language Server, managing one LSP client.
class LanguageServer final: public lsp::Server
{
public:
	using Logger = std::function<void(std::string_view)>;
	using PublishDiagnosticsList = std::vector<lsp::protocol::PublishDiagnosticsParams>;

	/// @param _logger special logger used for debugging the LSP.
	explicit LanguageServer(lsp::Transport& _client, Logger _logger);

	std::vector<boost::filesystem::path>& allowedDirectories() noexcept { return m_allowedDirectories; }

	// Client-to-Server messages
	void operator()(lsp::protocol::CancelRequest const&) override;
	void operator()(lsp::protocol::DidChangeTextDocumentParams const&) override;
	void operator()(lsp::protocol::DidCloseTextDocumentParams const&) override;
	void operator()(lsp::protocol::DidOpenTextDocumentParams const&) override;
	void operator()(lsp::protocol::InitializeRequest const&) override;
	void operator()(lsp::protocol::InitializedNotification const&) override;
	void operator()(lsp::protocol::ShutdownParams const&) override;
	void operator()(lsp::protocol::DefinitionParams const&) override;
	// TODO more to come :-)

	/// performs a validation run.
	///
	/// update diagnostics and also pushes any updates to the client.
	void validateAll();
	void validate(lsp::vfs::File const& _file, PublishDiagnosticsList& _result);
	void validate(lsp::vfs::File const& _file);

private:
	frontend::ReadCallback::Result readFile(std::string const&, std::string const&);

	void compile(lsp::vfs::File const& _file);

	frontend::ASTNode const* findASTNode(lsp::Position const& _position, std::string const& _fileName);

	std::optional<lsp::Range> declarationPosition(frontend::Declaration const* _declaration);

private:
	/// In-memory filesystem for each opened file.
	///
	/// Closed files will not be removed as they may be needed for compiling.
	lsp::vfs::VFS m_vfs;

	std::unique_ptr<FileReader> m_fileReader;

	/// map of input files to source code strings
	std::map<std::string, std::string> m_sourceCodes;

	/// Mapping between VFS file and its diagnostics.
	std::map<std::string /*URI*/, PublishDiagnosticsList> m_diagnostics;

	std::unique_ptr<frontend::CompilerStack> m_compilerStack;

	/// Allowed directories
	std::vector<boost::filesystem::path> m_allowedDirectories;

	/// Workspace root directory
	boost::filesystem::path m_basePath;
};

} // namespace solidity

