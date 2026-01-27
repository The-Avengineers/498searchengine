test_url() {
  if cmp -s <(./LinuxGetUrl "$1") <(curl -s "$1"); then
    echo -e "\x1b[32mPASSED\x1b[0m : $1"
  else
    echo -e "\x1b[31mFAILED\x1b[0m : $1"
  fi
}

test_ssl() {
  if cmp -s <(./LinuxGetSsl "$1") <(curl -sk "$1"); then
    echo -e "\x1b[32mPASSED\x1b[0m : $1"
  else
    echo -e "\x1b[31mFAILED\x1b[0m : $1"
  fi
}

test_url http://httpforever.com
test_url http://nytimes.com
test_url http://httpbin.org
test_url http://captive.apple.com
test_url http://www.faqs.org/faqs
test_url http://icio.us/
test_url http://web.eecs.umich.edu/
test_url http://web.eecs.umich.edu/~nham/
test_url http://web.eecs.umich.edu/~nham/CourseEvals/index.htm
test_url http://www.china.com.cn/

test_ssl https://www.debian.org/
test_ssl https://www.usps.com
test_ssl https://www.cdc.gov/index.html
test_ssl https://www.science.gov/index.html
test_ssl https://en.wikipedia.org/wiki/Main_Page
test_ssl https://www.umich.edu
test_ssl https://www.mit.edu
test_ssl https://www.stanford.edu
test_ssl https://web.eecs.umich.edu/~nham/
test_ssl https://hamiltonlabs.com
