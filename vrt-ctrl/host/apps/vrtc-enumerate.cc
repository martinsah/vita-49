#include <iostream>
#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <vrtc/ports.h>
#include <vrtc/datagram_buffer.h>
#include <vrtc/expr.h>
#include <vrtc/host_prims.h>
#include "udp_connection.h"
#include <cstdio>

static void
print_hex (FILE *fp, unsigned char *buf, int len)
{
  for (int i = 0; i < len; i++){
    fprintf (fp, "%02x ", buf[i]);
  }
  fprintf (fp, "\n");
}

extern "C" void expr_conn_send_datagram(void *handle, const void *buf, size_t len);

class expr_connection : public vrtc::udp_connection
{
  char 			d_rx_data[MAX_PAYLOAD];
  char 			d_tx_data[MAX_PAYLOAD];
  datagram_buffer_t	d_datagram_buffer;	// buffers partially constructed datagram
  boost::function<
    void (const boost::system::error_code &error, const Expr_t *expr)> d_expr_handler;
  
  void handle_rcvd_payload(const boost::system::error_code &error,
			   std::size_t bytes_transferred)
  {
    if (error){
      // FIXME, print it out
      std::cerr << "expr_connection: handle_rcvd_payload: error... "
		<< std::endl;
    }
    else {
      decode_payload(d_rx_data, bytes_transferred);
      
      // Fire off next async receive
      async_receive(boost::asio::buffer(d_rx_data, MAX_PAYLOAD),
		    boost::bind(&expr_connection::handle_rcvd_payload, this,
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred));
    }
  }

  void decode_payload(const void *payload, std::size_t len)
  {
    // Decode all Expr's in payload.
    // Call d_expr_handler for each one

    print_hex(stdout, (unsigned char *) payload, len);

  }

public:
  /*!
   * \brief Establish a control connection to a VRT device.
   *
   * \param io_service The io_service object that the expr_connection will use
   * to dispatch handlers for all asynchrnous operations performed.
   *
   * \param hostname   The hostname or numeric IP address of the device
   *
   * \param handler The handler to be called on every Expr_t received.
   * Copies will be made of the handler as required.  The function
   * signature of the handler must be:
   * <code> void handler(const boost::system::error_code &error, const Expr_t *e); </code>
   */
  template<typename ExprHandler>
  expr_connection(boost::asio::io_service &io,
		  const std::string &hostname,
		  ExprHandler handler)
    : vrtc::udp_connection(io, hostname),
      d_expr_handler(handler)
  {
    datagram_buffer_init(&d_datagram_buffer, d_tx_data, sizeof(d_tx_data),
			 expr_conn_send_datagram, (void *) this);
    
    // Fire off the first async receive
    async_receive(boost::asio::buffer(d_rx_data, MAX_PAYLOAD),
		  boost::bind(&expr_connection::handle_rcvd_payload, this,
			      boost::asio::placeholders::error,
			      boost::asio::placeholders::bytes_transferred));
  }

  ~expr_connection()
  {
    flush();
  }

  //! Encode expr and insert in outgoing datagram
  bool encode_and_enqueue(Expr_t *e)
  {
    return vrtc_encode(e, &d_datagram_buffer);
  }

  //! Encode expr, insert in outgoing datagram, and send it on its way
  bool encode_and_flush(Expr_t *e)
  {
    return encode_and_enqueue(e) && flush();
  }

  //! Send any partial datagram on its way
  bool flush() {
    return datagram_buffer_flush(&d_datagram_buffer);
  }

};

extern "C" {
  void expr_conn_send_datagram(void *handle, const void *buf, size_t len)
  {
    expr_connection *conn = (expr_connection *) handle;
    boost::system::error_code ignored_error;
    // FIXME async_send
    conn->send(boost::asio::buffer(buf, len), ignored_error);
  }
}

// ------------------------------------------------------------------------

class control {
  expr_connection d_conn;

public:
  control(boost::asio::io_service &io_service,
	  const std::string &hostname)
    : d_conn(io_service, hostname, boost::bind(&control::handle_rcvd_expr, this, _1, _2))
  {
    send_packet();
  }

  int alloc_inv_id()
  {
    return 0;	// FIXME
  }

  void free_inv_id(int inv_id)
  {
    // FIXME
  }

  void
  send_packet()
  {
    Expr_t *e = vrtc_make_get(alloc_inv_id(), "/");
    d_conn.encode_and_flush(e);
    vrtc_free_expr(e);
  }

  void
  handle_rcvd_expr(const boost::system::error_code &error, const Expr_t *e)
  {
    send_packet();
  }
};

int main(int argc, char* argv[])
{
  try {
    if (argc != 2)
      {
	std::cerr << "Usage: client <host>" << std::endl;
	return 1;
      }

    boost::asio::io_service io_service;
    control ctrl(io_service, argv[1]);

    io_service.run();
  }
  catch (std::exception& e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
