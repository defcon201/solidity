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
#include <boost/algorithm/string/predicate.hpp>
#include <solls/LanguageServer.h>

#include <liblsp/OutputGenerator.h>
#include <liblsp/protocol.h>

#include <libsolidity/ast/AST.h>
#include <libsolidity/ast/ASTVisitor.h>
#include <libsolidity/interface/ReadFile.h>

#include <liblangutil/SourceReferenceExtractor.h>

#include <libsolutil/Visitor.h>
#include <libsolutil/JSON.h>

#include <boost/filesystem.hpp>

#include <ostream>

#include <iostream>
#include <string>

using namespace std;
using namespace std::placeholders;

using namespace solidity::langutil;
using namespace solidity::frontend;

namespace solidity {

LanguageServer::LanguageServer(lsp::Transport& _client, Logger _logger):
	lsp::Server(_client, std::move(_logger)),
	m_vfs()
{
}

void LanguageServer::operator()(lsp::protocol::CancelRequest const& _args)
{
	auto const id = visit(util::GenericVisitor{
		[](string const& _id) -> string { return _id; },
		[](int _id) -> string { return to_string(_id); }
	}, _args.id);

	logInfo("LanguageServer: Request " + id + " cancelled.");
}

void LanguageServer::operator()(lsp::protocol::ShutdownParams const&)
{
	logInfo("LanguageServer: shutdown requested");
}

void LanguageServer::operator()(lsp::protocol::InitializeRequest const& _args)
{
#if !defined(NDEBUG)
	ostringstream msg;
	msg << "LanguageServer: Initializing, PID :" << _args.processId.value_or(-1) << endl;
	msg << "                rootUri           : " << _args.rootUri.value_or("NULL") << endl;
	msg << "                rootPath          : " << _args.rootPath.value_or("NULL") << endl;
	for (auto const& workspace: _args.workspaceFolders)
		msg << "                workspace folder: " << workspace.name << "; " << workspace.uri << endl;
#endif

	if (boost::starts_with(_args.rootUri.value_or(""), "file:///"))
	{
		auto const fspath = boost::filesystem::path(_args.rootUri.value().substr(7));
		m_basePath = fspath;
		m_allowedDirectories.push_back(fspath);
	}

#if !defined(NDEBUG)
	logMessage(msg.str());
#endif

	lsp::protocol::InitializeResult result;
	result.requestId = _args.requestId;
	result.capabilities.hoverProvider = true;
	result.capabilities.textDocumentSync.openClose = true;
	result.capabilities.textDocumentSync.change = lsp::protocol::TextDocumentSyncKind::Incremental;
	result.capabilities.definitionProvider = true; // go-to-definition feature

	reply(_args.requestId, result);
}

void LanguageServer::operator()(lsp::protocol::InitializedNotification const&)
{
	// NB: this means the client has finished initializing. Now we could maybe start sending
	// events to the client.
	logMessage("LanguageServer: Client initialized");
}

void LanguageServer::operator()(lsp::protocol::DidOpenTextDocumentParams const& _args)
{
	logMessage("LanguageServer: Opening document: " + _args.textDocument.uri);

	lsp::vfs::File const& file = m_vfs.insert(
		_args.textDocument.uri,
		_args.textDocument.languageId,
		_args.textDocument.version,
		_args.textDocument.text
	);

	validate(file);
}

void LanguageServer::operator()(lsp::protocol::DidChangeTextDocumentParams const& _didChange)
{
	if (lsp::vfs::File* file = m_vfs.find(_didChange.textDocument.uri); file != nullptr)
	{
		if (_didChange.textDocument.version.has_value())
			file->setVersion(_didChange.textDocument.version.value());

		for (lsp::protocol::TextDocumentContentChangeEvent const& contentChange: _didChange.contentChanges)
		{
			visit(util::GenericVisitor{
				[&](lsp::protocol::TextDocumentRangedContentChangeEvent const& change) {
#if !defined(NDEBUG)
					ostringstream str;
					str << "did change: " << change.range << " for '" << change.text << "'";
					logMessage(str.str());
#endif
					file->modify(change.range, change.text);
				},
				[&](lsp::protocol::TextDocumentFullContentChangeEvent const& change) {
					file->replace(change.text);
				}
			}, contentChange);
		}

		validate(*file);
	}
	else
		logError("LanguageServer: File to be modified not opened \"" + _didChange.textDocument.uri + "\"");
}

void LanguageServer::operator()(lsp::protocol::DidCloseTextDocumentParams const& _didClose)
{
	logMessage("LanguageServer: didClose: " + _didClose.textDocument.uri);
}

void LanguageServer::validateAll()
{
	for (reference_wrapper<lsp::vfs::File const> const& file: m_vfs.files())
		validate(file.get());
}

void LanguageServer::validate(lsp::vfs::File const& _file)
{
	PublishDiagnosticsList result;
	validate(_file, result);

	for (lsp::protocol::PublishDiagnosticsParams const& diag: result)
		notify(diag);
}

frontend::ReadCallback::Result LanguageServer::readFile(string const& _kind, string const& _path)
{
	return m_fileReader->readFile(_kind, _path);
}

constexpr lsp::protocol::DiagnosticSeverity toDiagnosticSeverity(Error::Type _errorType)
{
	switch (_errorType)
	{
		case Error::Type::CodeGenerationError:
		case Error::Type::DeclarationError:
		case Error::Type::DocstringParsingError:
		case Error::Type::ParserError:
		case Error::Type::SyntaxError:
		case Error::Type::TypeError:
			return lsp::protocol::DiagnosticSeverity::Error;
		case Error::Type::Warning:
			return lsp::protocol::DiagnosticSeverity::Warning;
	}
	// Should never be reached.
	return lsp::protocol::DiagnosticSeverity::Error;
}

void LanguageServer::compile(lsp::vfs::File const& _file)
{
	// TODO: optimize! do not recompile if nothing has changed (file(s) not flagged dirty).

	// always start fresh when compiling
	m_sourceCodes.clear();

	m_sourceCodes[_file.uri().substr(7)] = _file.contentString();

	m_fileReader = make_unique<FileReader>(m_basePath, m_allowedDirectories);

	m_compilerStack.reset();
	m_compilerStack = make_unique<CompilerStack>(bind(&FileReader::readFile, ref(*m_fileReader), _1, _2));

	// TODO: configure all compiler flags like in CommandLineInterface (TODO: refactor to share logic!)
	OptimiserSettings settings = OptimiserSettings::standard(); // TODO: get from config
	m_compilerStack->setOptimiserSettings(settings);
	m_compilerStack->setParserErrorRecovery(true);
	m_compilerStack->setEVMVersion(EVMVersion::constantinople()); // TODO: get from config
	m_compilerStack->setRevertStringBehaviour(RevertStrings::Default); // TODO get from config
	m_compilerStack->setSources(m_sourceCodes);

	m_compilerStack->compile();
}

void LanguageServer::validate(lsp::vfs::File const& _file, PublishDiagnosticsList& _result)
{
	compile(_file);

	lsp::protocol::PublishDiagnosticsParams params{};
	params.uri = _file.uri();

	for (shared_ptr<Error const> const& error: m_compilerStack->errors())
	{
		// Don't show this warning. "This is a pre-release compiler version."
		if (error->errorId().error == 3805)
			continue;

		auto const message = SourceReferenceExtractor::extract(*error);

		auto const severity = toDiagnosticSeverity(error->type());

		// global warnings don't have positions in the source code - TODO: default them to top of file?

		auto const startPosition = LineColumn{{
			max(message.primary.position.line, 0),
			max(message.primary.startColumn, 0)
		}};

		auto const endPosition = LineColumn{{
			max(message.primary.position.line, 0),
			max(message.primary.endColumn, 0)
		}};

		lsp::protocol::Diagnostic diag{};
		diag.range.start.line = startPosition.line;
		diag.range.start.column = startPosition.column;
		diag.range.end.line = endPosition.line;
		diag.range.end.column = endPosition.column;
		diag.message = message.primary.message;
		diag.source = "solc";
		diag.severity = severity;

		for (SourceReference const& secondary: message.secondary)
		{
			auto related = lsp::protocol::DiagnosticRelatedInformation{};

			related.message = secondary.message;
			related.location.uri = "file://" + secondary.sourceName; // is the sourceName always a fully qualified path?
			related.location.range.start.line = secondary.position.line;
			related.location.range.start.column = secondary.startColumn;
			related.location.range.end.line = secondary.position.line; // what about multiline?
			related.location.range.end.column = secondary.endColumn;

			diag.relatedInformation.emplace_back(move(related));
		}

		if (message.errorId.has_value())
			diag.code = to_string(message.errorId.value().error);

		params.diagnostics.emplace_back(move(diag));
	}

	// some additional analysis (as proof of concept)
#if 1
	for (size_t pos = _file.contentString().find("FIXME", 0); pos != string::npos; pos = _file.contentString().find("FIXME", pos + 1))
	{
		lsp::protocol::Diagnostic diag{};
		diag.message = "Hello, FIXME's should be fixed.";
		diag.range.start = _file.buffer().toPosition(pos);
		diag.range.end = {diag.range.start.line, diag.range.start.column + 5};
		diag.severity = lsp::protocol::DiagnosticSeverity::Error;
		diag.source = "solc";
		params.diagnostics.emplace_back(diag);
	}

	for (size_t pos = _file.contentString().find("TODO", 0); pos != string::npos; pos = _file.contentString().find("FIXME", pos + 1))
	{
		lsp::protocol::Diagnostic diag{};
		diag.message = "Please remember to create a ticket on GitHub for that.";
		diag.range.start = _file.buffer().toPosition(pos);
		diag.range.end = {diag.range.start.line, diag.range.start.column + 5};
		diag.severity = lsp::protocol::DiagnosticSeverity::Hint;
		diag.source = "solc";
		params.diagnostics.emplace_back(diag);
	}
#endif

	_result.emplace_back(params);
}

// TOOD: maybe use SimpleASTVisitor here, if that would be a simple free-fuunction :)
class ASTNodeLocator : public ASTConstVisitor
{
private:
	int m_pos = -1;
	ASTNode const* m_currentNode = nullptr;

public:
	explicit ASTNodeLocator(int _pos): m_pos{_pos}
	{
	}

	ASTNode const* closestMatch() const noexcept { return m_currentNode; }

	bool visitNode(ASTNode const& _node) override
	{
		if (_node.location().start <= m_pos && m_pos <= _node.location().end)
		{
			m_currentNode = &_node;
			return true;
		}
		return false;
	}
};

frontend::ASTNode const* LanguageServer::findASTNode(lsp::Position const& _position, std::string const& _fileName)
{
	if (!m_compilerStack)
		return nullptr;

	frontend::ASTNode const& sourceUnit = m_compilerStack->ast(_fileName);
	auto const sourcePos = sourceUnit.location().source->translateLineColumnToPosition(_position.line + 1, _position.column + 1);

	ASTNodeLocator m{sourcePos};
	sourceUnit.accept(m);
	auto const closestMatch = m.closestMatch();
	if (closestMatch != nullptr)
		fprintf(stderr, "findASTNode for %s @ pos=%d:%d (%d), symbol: '%s' (%s)\n",
				sourceUnit.location().source->name().c_str(),
				sourcePos,
				_position.line,
				_position.column,
				closestMatch->location().text().c_str(), typeid(*closestMatch).name());
	else
		fprintf(stderr, "findASTNode for pos=%d:%d (%d), not found.\n",
				sourcePos,
				_position.line,
				_position.column);

	return closestMatch;
}

void LanguageServer::operator()(lsp::protocol::DefinitionParams const& _params)
{
	lsp::protocol::DefinitionReplyParams output{};

	if (auto const file = m_vfs.find(_params.textDocument.uri); file != nullptr)
	{
		compile(*file);
		solAssert(m_compilerStack.get() != nullptr, "");

		auto const sourceName = file->uri().substr(7); // strip "file://"

		if (auto const sourceNode = findASTNode(_params.position, sourceName); sourceNode)
		{
			if (auto const sourceIdentifier = dynamic_cast<Identifier const*>(sourceNode); sourceIdentifier != nullptr)
			{
				auto const declaration = !sourceIdentifier->annotation().candidateDeclarations.empty()
					? sourceIdentifier->annotation().candidateDeclarations.front()
					: sourceIdentifier->annotation().referencedDeclaration;

				if (auto const loc = declarationPosition(declaration); loc.has_value())
				{
					output.range = loc.value();
					output.uri = "file://" + declaration->location().source->name();
					reply(_params.requestId, output);
				}
				else
					error(_params.requestId, lsp::protocol::ErrorCode::InvalidParams, "Declaration not found.");
			}
			else if (auto const n = dynamic_cast<frontend::MemberAccess const*>(sourceNode); n)
			{
				auto const declaration = n->annotation().referencedDeclaration;

				if (auto const loc = declarationPosition(declaration); loc.has_value())
				{
					auto const sourceName = declaration->location().source->name();
					auto const fullSourceName = m_fileReader->fullPathMapping().at(sourceName);
					output.range = loc.value();
					output.uri = "file://" + fullSourceName;
					reply(_params.requestId, output);
				}
				else
					error(_params.requestId, lsp::protocol::ErrorCode::InvalidParams, "Declaration not found.");
			}
			else
			{
				fprintf(stderr, "identifier: %s\n", typeid(*sourceIdentifier).name());
				error(_params.requestId, lsp::protocol::ErrorCode::InvalidParams, "Symbol is not an identifier.");
			}
		}
		else
		{
			error(_params.requestId, lsp::protocol::ErrorCode::InvalidParams, "Symbol not found.");
		}
	}
	else
		error(_params.requestId, lsp::protocol::ErrorCode::InvalidRequest, "File not found in VFS.");
}

optional<lsp::Range> LanguageServer::declarationPosition(frontend::Declaration const* _declaration)
{
	if (!_declaration)
		return nullopt;

	auto const location = _declaration->location();

	auto const [startLine, startColumn] = location.source->translatePositionToLineColumn(location.start);
	auto const [endLine, endColumn] = location.source->translatePositionToLineColumn(location.end);

	return lsp::Range{
		lsp::Position{startLine, startColumn},
		lsp::Position{endLine, endColumn}
	};
}

// TOOD: maybe use SimpleASTVisitor here, if that would be a simple free-fuunction :)
class ReferenceCollector: public frontend::ASTConstVisitor
{
private:
	frontend::ASTNode const& m_scope;
	frontend::Declaration const& m_declaration;
	std::vector<lsp::protocol::DocumentHighlight> m_result;

public:
	ReferenceCollector(ASTNode const& _scope, frontend::Declaration const& _declaration):
		m_scope{_scope},
		m_declaration{_declaration}
	{
		fprintf(stderr, "finding decl: %s\n", _declaration.name().c_str());
	}

	std::vector<lsp::protocol::DocumentHighlight> take() { return std::move(m_result); }

	bool visit(Identifier const& _identifier) override
	{
		if (auto const declaration = _identifier.annotation().referencedDeclaration; declaration)
		{
			if (declaration == &m_declaration)
			{
				auto const& location = _identifier.location();
				auto const [startLine, startColumn] = location.source->translatePositionToLineColumn(location.start);
				auto const [endLine, endColumn] = location.source->translatePositionToLineColumn(location.end);
				auto const locationRange = lsp::Range{
					lsp::Position{startLine, startColumn},
					lsp::Position{endLine, endColumn}
				};
				fprintf(stderr, " -> found at %d:%d .. %d:%d\n",
						startLine, startColumn,
						endLine, endColumn
				);

				auto highlight = lsp::protocol::DocumentHighlight{};
				highlight.range = locationRange;
				highlight.kind = lsp::protocol::DocumentHighlightKind::Text; // TODO: are you being read or written to?
				m_result.emplace_back(highlight);

				return true;
			}
		}
		return true;
	}

	// TODO: MemberAccess
	// TODO: function declarations?

	bool visitNode(ASTNode const& _node) override
	{
		(void) _node;
		(void) m_scope;
		(void) m_declaration;

		return true;
	}
};

std::vector<lsp::protocol::DocumentHighlight> LanguageServer::findAllReferences(frontend::Declaration const* _declaration)
{
	if (_declaration)
	{
		auto const scope = _declaration->annotation().contract;
		auto collector = ReferenceCollector(*scope, *_declaration);
		scope->accept(collector);
		return collector.take();
	}

	return {};
}

void LanguageServer::operator()(lsp::protocol::DocumentHighlightParams const& _params)
{
	auto output = lsp::protocol::DocumentHighlightReplyParams{};

	fprintf(stderr, "DocumentHighlightParams: %s:%d:%d\n",
		_params.textDocument.uri.c_str(),
		_params.position.line,
		_params.position.column
	);

	if (auto const file = m_vfs.find(_params.textDocument.uri); file != nullptr)
	{
		compile(*file);
		solAssert(m_compilerStack.get() != nullptr, "");

		auto const sourceName = file->uri().substr(7); // strip "file://"

		if (auto const sourceNode = findASTNode(_params.position, sourceName); sourceNode)
		{
			if (auto const sourceIdentifier = dynamic_cast<Identifier const*>(sourceNode); sourceIdentifier != nullptr)
			{
				auto const declaration = !sourceIdentifier->annotation().candidateDeclarations.empty()
					? sourceIdentifier->annotation().candidateDeclarations.front()
					: sourceIdentifier->annotation().referencedDeclaration;

				auto output = lsp::protocol::DocumentHighlightReplyParams{};
				output.highlights = findAllReferences(declaration);

				reply(_params.requestId, output);
			}
			else
			{
				fprintf(stderr, "identifier: %s\n", typeid(*sourceIdentifier).name());
				error(_params.requestId, lsp::protocol::ErrorCode::InvalidParams, "Symbol is not an identifier.");
			}
		}
		else
		{
			error(_params.requestId, lsp::protocol::ErrorCode::InvalidParams, "Symbol not found.");
		}
	}

	// reply(_params.requestId, output);
	error(_params.requestId, lsp::protocol::ErrorCode::RequestCancelled, "not implemented yet.");
}

} // namespace solidity
