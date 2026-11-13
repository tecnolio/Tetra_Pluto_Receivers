// Shared HTTP/HTTPS gateway. RF processes remain independent.
package main

import (
	"bytes"
 "bufio"
 "net"
 "sync"
	"crypto/subtle"
	"crypto/tls"
	"encoding/base64"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/http/httputil"
	"net/url"
	"os"
	"regexp"
	"strconv"
	"strings"
	"time"
)

var rootReference = regexp.MustCompile("([\"'`])/([^/])")

func main() {
	listen := flag.String("listen", ":443", "HTTPS listener")
	cert := flag.String("cert", "server.crt", "server certificate")
	key := flag.String("key", "server.key", "server private key")
	ca := flag.String("ca", "pluto-ca.crt", "public CA certificate")
	plain := flag.String("http-listen", "", "optional HTTP listener; production HTTP is served natively on 8080")
	backend := flag.String("backend", "http://127.0.0.1:8080", "native web server")
	dual := flag.Bool("dual-protocol", false, "accept HTTP and HTTPS on the main listener")
 extras := flag.Bool("extra-services", false, "also serve TETRAPOL on 8443 and ADS-B on 8444")
	flag.Parse()
	target, err := url.Parse(*backend)
	if err != nil {
		log.Fatal(err)
	}
	root, err := os.ReadFile(*ca)
	if err != nil {
		log.Fatal(err)
	}
	makeHandler := func(target *url.URL, prefix string) http.Handler {
		proxy := &httputil.ReverseProxy{
			Rewrite: func(r *httputil.ProxyRequest) {
				if prefix != "" {
					r.Out.URL.Path = strings.TrimPrefix(r.Out.URL.Path, strings.TrimSuffix(prefix, "/"))
					r.Out.URL.RawPath = ""
				}
				r.SetURL(target)
				r.Out.Header.Set("Accept-Encoding", "identity")
				r.Out.Host = r.In.Host
				r.SetXForwarded()
			},
			ModifyResponse: func(response *http.Response) error {
				if prefix == "" {
					return nil
				}
				if loc := response.Header.Get("Location"); strings.HasPrefix(loc, "/") && !strings.HasPrefix(loc, "//") {
					response.Header.Set("Location", strings.TrimSuffix(prefix, "/")+loc)
				}
				// Only application HTML/JS and CSS need rebasing; JSON, streams, libraries and images pass through.
				mime := response.Header.Get("Content-Type")
				path := response.Request.URL.Path
				if !strings.Contains(mime, "text/html") && path != "/app.js" && !strings.Contains(mime, "text/css") {
					return nil
				}
				body, err := io.ReadAll(io.LimitReader(response.Body, 4*1024*1024+1))
				if err != nil {
					return err
				}
				response.Body.Close()
				if len(body) > 4*1024*1024 {
					return fmt.Errorf("application asset exceeds rewrite limit")
				}
				text := string(body)
				text = rootReference.ReplaceAllString(text, "${1}"+prefix+"${2}")
				text = strings.ReplaceAll(text, "url(/", "url("+prefix)
				text = strings.ReplaceAll(text, "http://127.0.0.1:8090/", prefix)
				body = []byte(text)
				response.Body = io.NopCloser(bytes.NewReader(body))
				response.ContentLength = int64(len(body))
				response.Header.Set("Content-Length", strconv.Itoa(len(body)))
				response.Header.Del("ETag")
				response.Header.Del("Last-Modified")
				return nil
			},
			FlushInterval: -1,
			Transport: &http.Transport{MaxIdleConns: 32, MaxIdleConnsPerHost: 16,
				IdleConnTimeout: 30 * time.Second, ResponseHeaderTimeout: 20 * time.Second,
				ExpectContinueTimeout: time.Second},
		}
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			w.Header().Set("Permissions-Policy", "geolocation=(self)")
			if r.URL.Path == "/pluto-ca.crt" {
				w.Header().Set("Content-Type", "application/x-x509-ca-cert")
				w.Header().Set("Content-Disposition", `attachment; filename="pluto-ca.crt"`)
				w.Write(root)
				return
			}
			// Protect every TLS listener, including services without backend authentication.
			token, err := os.ReadFile("/mnt/jffs2/pluto_lab/web-auth")
			user, password, ok := r.BasicAuth()
			presented := base64.StdEncoding.EncodeToString([]byte(user + ":" + password))
			if err != nil || !ok || subtle.ConstantTimeCompare([]byte(presented), []byte(strings.TrimSpace(string(token)))) != 1 {
				w.Header().Set("WWW-Authenticate", `Basic realm="Pluto Launchpad"`)
				w.Header().Set("Cache-Control", "no-store")
				http.Error(w, "Autenticacion requerida", http.StatusUnauthorized)
				return
			}
			proxy.ServeHTTP(w, r)
		})
	}
	makeServer := func(address, upstream string) *http.Server {
		u, err := url.Parse(upstream)
		if err != nil {
			log.Fatal(err)
		}
		return &http.Server{Addr: address, Handler: makeHandler(u, ""),
			ReadHeaderTimeout: 10 * time.Second, IdleTimeout: 60 * time.Second,
			MaxHeaderBytes: 32768,
			TLSConfig:      &tls.Config{MinVersion: tls.VersionTLS12},
		}
	}
	if *extras {
		for _, pair := range []string{":8444|http://127.0.0.1:8090"} {
			parts := strings.SplitN(pair, "|", 2)
			s := makeServer(parts[0], parts[1])
			go func() {
				if err := s.ListenAndServeTLS(*cert, *key); err != nil {
					log.Printf("Extra HTTPS listener %s: %v", s.Addr, err)
				}
			}()
		}
	}
	// Wrappers stay at /<project>; their embedded applications and every dependency use this prefix.
	shared := http.NewServeMux()
	shared.Handle("/", makeHandler(target, ""))
	for _, project := range []struct{ prefix, upstream string }{{"/adsb/app/", "http://127.0.0.1:8090"}} {
		u, _ := url.Parse(project.upstream)
		shared.Handle(project.prefix, makeHandler(u, project.prefix))
	}
	if *plain != "" {
		s := makeServer(*plain, target.String())
		s.Handler = shared
		go func() { log.Fatal(s.ListenAndServe()) }()
	}
	server := makeServer(*listen, target.String())
	server.Handler = shared
	log.Printf("Pluto HTTPS listening on %s, backend %s", *listen, *backend)
	if *dual {
        pair, err := tls.LoadX509KeyPair(*cert, *key)
        if err != nil { log.Fatal(err) }
        config := &tls.Config{Certificates: []tls.Certificate{pair}, MinVersion: tls.VersionTLS12, NextProtos: []string{"http/1.1"}}
        listener, err := net.Listen("tcp", *listen)
        if err != nil { log.Fatal(err) }
        log.Fatal(server.Serve(newDualListener(listener, config)))
    }
    log.Fatal(server.ListenAndServeTLS(*cert, *key))
}

// Classify connections concurrently so a silent client cannot block Accept.
// Keep HTTP available for existing control clients while adding TLS on the same port.
type bufferedConn struct { net.Conn; reader *bufio.Reader }
func (c *bufferedConn) Read(p []byte) (int,error) { return c.reader.Read(p) }
type acceptResult struct { conn net.Conn; err error }
type dualListener struct {
 net.Listener
 ready chan acceptResult
 done chan struct{}
 once sync.Once
}
func newDualListener(inner net.Listener, config *tls.Config) *dualListener {
 d := &dualListener{Listener:inner, ready:make(chan acceptResult), done:make(chan struct{})}
 go func() {
  slots:=make(chan struct{},64)
  for {
   select { case slots<-struct{}{}: case <-d.done:return }
   c,err:=inner.Accept()
   if err!=nil { <-slots; select {case d.ready<-acceptResult{err:err}:case <-d.done:}; return }
   go func(c net.Conn) {
    defer func(){<-slots}()
    c.SetReadDeadline(time.Now().Add(10*time.Second))
    reader:=bufio.NewReader(c)
    first,err:=reader.Peek(1)
    if err!=nil { c.Close();return }
    c.SetReadDeadline(time.Time{})
    var wrapped net.Conn=&bufferedConn{Conn:c,reader:reader}
    if first[0]==0x16 { wrapped=tls.Server(wrapped,config) }
    select {case d.ready<-acceptResult{conn:wrapped}:case <-d.done:c.Close()}
   }(c)
  }
 }()
 return d
}
func (d *dualListener) Accept()(net.Conn,error) {
 select {case result:=<-d.ready:return result.conn,result.err;case <-d.done:return nil,net.ErrClosed}
}
func (d *dualListener) Close()error {
 var err error
 d.once.Do(func(){close(d.done);err=d.Listener.Close()})
 return err
}
