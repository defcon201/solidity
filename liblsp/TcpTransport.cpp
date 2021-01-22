#include <liblsp/TcpTransport.h>
#include <liblsp/TcpTransport.h>

namespace lsp {

using std::nullopt;
using std::optional;
using std::to_string;

using namespace std::string_literals;

TcpTransport::TcpTransport(unsigned short _port, std::function<void(std::string_view)> _trace):
	m_io_service(),
	m_endpoint(boost::asio::ip::make_address("127.0.0.1"), _port),
	m_acceptor(m_io_service),
	m_stream(),
	m_jsonTransport(),
	m_trace(std::move(_trace))
{
	m_acceptor.open(m_endpoint.protocol());
	m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
	m_acceptor.bind(m_endpoint);
	m_acceptor.listen();
	m_trace("Listening on tcp://127.0.0.1:"s + to_string(_port));
}

bool TcpTransport::closed() const noexcept
{
	return !m_acceptor.is_open();
}

optional<Json::Value> TcpTransport::receive()
{
	auto const clientClosed = [&]() { return !m_stream || !m_stream.value().good() || m_stream.value().eof(); };

	if (clientClosed())
	{
		m_stream.emplace(m_acceptor.accept());
		if (clientClosed())
			return nullopt;

		auto const remoteAddr = m_stream.value().socket().remote_endpoint().address().to_string();
		auto const remotePort = m_stream.value().socket().remote_endpoint().port();
		m_trace("New client connected from "s + remoteAddr + ":" + to_string(remotePort) + ".");
		m_jsonTransport.emplace(m_stream.value(), m_stream.value(), [this](auto msg) { if (m_trace) m_trace(msg); });
	}
	if (auto value = m_jsonTransport.value().receive(); value.has_value())
		return value;

	if (clientClosed())
	{
		m_trace("Client disconnected.");
		m_jsonTransport.reset();
		m_stream.reset();
	}
	return nullopt;
}

void TcpTransport::notify(std::string const& _method, Json::Value const& _params)
{
	if (m_jsonTransport.has_value())
		m_jsonTransport.value().notify(_method, _params);
}

void TcpTransport::reply(MessageId const& _id, Json::Value const& _result)
{
	if (m_jsonTransport.has_value())
		m_jsonTransport.value().reply(_id, _result);
}

void TcpTransport::error(MessageId const& _id, ErrorCode _code, std::string const& _message)
{
	if (m_jsonTransport.has_value())
		m_jsonTransport.value().error(_id, _code, _message);
}

}
