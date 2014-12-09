#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>

#define AIMROUNDS  256
#define AIMAX      512
#define AIMLEN     1024
#define RAND(x)    ((x)?(random()%(x)):0)
#define MIN(a, b)  (((a) < (b)) ? a : b)
#define BUFSIZE    4096
#define LOGM(name) if(verbose) fprintf(stderr, " %s", name)

ssize_t write(int fd, const void *buf, size_t count);

int output;
int count;
int verbose;
char *output_pattern;

char *store;
size_t stend;
char **samples;
int nsamples;
int seeded;
unsigned int seed;

void random_block(size_t orig_len) {
   struct stat st;
   int fd;
   size_t start, len;
   char *path;
   path = samples[RAND(nsamples)];
   fd = open(path, O_RDONLY);
   if (fd < 0) {
      fprintf(stderr, "Cannot open %s\n", path);
      exit(1);
   }
   if (stat(path, &st) < 0) {
      fprintf(stderr, "Error accessing %s\n", path);
      exit(1);
   }
   if (st.st_size < 3) {
      close(fd);
      return;
   }
   if (store)
      free(store);
   start = RAND(st.st_size-2);
   len = st.st_size - start;
   if (len > 4 * orig_len) 
      len = 4 * orig_len;
   len = RAND(len);
   lseek(fd, start, SEEK_SET);
   store = malloc(len);
   if (!store) {
      fprintf(stderr, "could no allocate %d bytes for data from %s\n", (int) len, path);
      exit(1);
   }
   stend = read(fd, store, len); /* partial reads are ok */
   close(fd);
   if (stend < 0) {
      fprintf(stderr, "could not read from %s\n", path);
      exit(1);
   }
   return;
}

/* buffer this locally to reduce write()s if ni ends up being use(d|ful) */
void write_all(const char *data, size_t n) {
   while (n > 0) {
      ssize_t wrote = write(output, data, n);
      if (wrote < 0) {
         perror("ni: write failure");
         exit(1);
      }
      n -= wrote;
   }
}

char* format_num(char *buff, size_t buflen, long long n) {
   int negp = 0;
   size_t p = buflen - 1;
   if (n < 0) {
      n *= -1;
      negp = 1;
   }
   if (n == 0) {
      buff[p--] = '0';
   } else {
      while(n) {
         if (!p)
            return NULL;
         buff[p--] = n % 10 + '0';
         n /= 10;
      }
      if (negp) {
         if (!p)
            return NULL;
         buff[p--] = '-';
      }
   }
   return(buff + p + 1);
}

/* construct a null terminated string to *res from null terminated pattern, or return NULL if errors occur */
char *format_path(char *buff, size_t buflen, char *pat, long long n) {
   char *pp = pat;
   char *bp = buff + buflen - 1;
   while (pp[1])
      pp++;
   *bp-- = 0;
   while (pp >= pat) {
      char c = *pp--;
      if (c == '%') {
         if (bp[1] == 'n') {
            bp += 2;
            bp = format_num(buff, bp - buff, n);
            if (!bp)
               return NULL;
            bp--;
         } else {
            fprintf(stderr, "bad pattern in path (did you mean %%n?)\n");
            return NULL;
         }
      } else {
         *bp-- = c;
      } 
      if (bp < buff) {
         fprintf(stderr, "path does not fit to buffer. sorry, or more likely you should be.\n");
         return NULL;
      }
   }
   return (bp + 1);
}

void output_num(char *buff, size_t buflen, long long n) {
   int negp = 0;
   if (n < 0) {
      n *= -1;
      negp = 1;
   }
   if (n == 0) {
      buff[0] = '0';
      write_all(buff, 1);
   } else {
      size_t p = buflen - 1;
      while(n && p) {
         buff[p--] = n % 10 + '0';
         n /= 10;
      }
      if (negp || !(random()&63))
         buff[p--] = '-';
      p++;
      write_all(buff + p, buflen - p);
   }
}

int sufscore(const char *a, size_t al, const char *b, size_t bl) {
   int last = 256;
   int n = 0;
   while(al-- && bl-- && *a == *b && n < AIMAX) {
      if (*a != last)
         n += 32;
      last = *a++;
      b++;
   }
   return n;
}

/* note, could have a separate aimer for runs */
void aim(const char *from, size_t fend, const char *to, size_t tend, size_t *jump, size_t *land) {
   size_t j, l;
   int best_score = 0, score, rounds = 0;
   if (!fend) { 
      *jump = 0; 
      *land = tend ? RAND(tend) : 0;
      return;
   }
   *jump = RAND(fend);
   if (!tend) {
      *land = 0;
      return;
   }
   *land = RAND(tend);
   rounds = RAND(AIMROUNDS);
   score = 0;
   while(rounds--) {
      int maxs = AIMLEN;
      j = RAND(fend);
      l = RAND(tend);
      while(maxs-- && l < tend && from[j] != to[l]) {
         l++;
      }
      score = sufscore(from + j, fend - j, to + l, tend - l);
      if (score > best_score) {
         best_score = score;
         *jump = j;
         *land = l;
      }
   }
}

void seek_num(const char *pos, size_t end, size_t *ns, size_t *ne) {
   size_t o = RAND(end);
   while(o < end && (pos[o] < '0' || pos[o] > '9')) {
      if (pos[o] & 128)
         return;
      o++;
   }
   if (o == end)
      return;
   *ns = o++;
   while(o < end && pos[o] >= '0' && pos[o] <= '9') {
      o++;
   }
   *ne = o;
}

int read_num(const char *pos, size_t end, long long *res) {
   long long n = 0;
   size_t p = 0;
   while(p < end) {
      n = n * 10 + pos[p++] - '0';
      if (n < 0)
         return 1;
   }
   *res = n;
   return 0;
}

long long twiddle(long long val) {
   do {
      switch(RAND(3)) {
         case 0:
            val = random();
            break;
         case 1: {
            val ^= (1 << RAND(sizeof(long long)*8 - 1));
            break; }
         case 2: {
            val += RAND(5) - 2;
            break;
         }
      }
   } while (random() & 1);
   return(val);
}


void mutate_area(const char *data, size_t end) {
   static char buff[BUFSIZE];
   retry:
   switch(random() % 26){
      case 0: { /* insert a random byte */
         size_t pos = (end ? random() % end : 0);
         write_all(data, pos);
         LOGM("ins");
         buff[0] = random() & 255;
         write_all(buff, 1);
         write_all(data + pos, end - pos);
         break; }
      case 1: { /* drop a byte */
         size_t pos = (end ? random() % end : 0);
         if (pos+1 >= end)
            goto retry;
         LOGM("drop");
         write_all(data, pos);
         write_all(data+pos+1, end-(pos+1));
         break; }
      case 2:
      case 3: { /* jump in a sequence */
         size_t s, e;
         if (!end) 
            goto retry;
         s = random() % end;
         e = random() % end;
         if (s == e) 
            goto retry;
         LOGM("jmp");
         write_all(data, e);
         write_all(data+s, end-s);
         break; }
      case 4:
      case 5: { /* repeat */
         size_t a, b, s, e, l;
         int n = 8;
         while (random() & 1 && n < 20000)
            n <<= 1;
         n = random() % n + 2;
         if (!end) 
            goto retry;
         a = (end ? random() % end : 0);
         b = (end ? random() % end : 0);
         if (a == b) {
            goto retry;
         } else if (a > b) {
            s = b; e = a;
         } else {
            s = a; e = b;
         }
         l = e - s;
         LOGM("rep");
         write_all(data, s);
         if (l * n > 134217728) {
            l = random() % 1024 + 2;
         }
         while(n--) {
            write_all(data+s, l);
         }
         write_all(data+s, end-s);
         break; }
      case 6: { /* insert random data */
         size_t pos = (end ? random() % end : 0);
         int n = random() % 1022 + 2;
         int p = 0;
         while (p < n) {
            buff[p++] = random() & 255;
         }
         LOGM("inss");
         write_all(data, pos);
         write_all(buff, p);
         write_all(data+pos, end-pos);
         break; }
      case 7:
      case 8:
      case 9:
      case 10:
      case 11:
      case 12: { /* aimed jump to self */
         size_t j=0, l=0;
         if (end < 5)
            goto retry;
         while (j == l)
            aim(data, end, data, end, &j, &l);
         LOGM("ajmp");
         write_all(data, j);
         write_all(data+l, end-l);
         break; }
      case 13:
      case 14:
      case 15:
      case 16:
      case 17: { /* aimed random block fusion */
         size_t j, l, dm, sm;
         char *buff;
         size_t bend;
         if (end < 8) goto retry;
         random_block(end);
         if (stend < 8)
            goto retry;
         dm = end >> 1;
         sm = stend >> 1;
         LOGM("arf");
         aim(data, dm, store, sm, &j, &l);
         write_all(data, j);
         data += j;
         end -= j;
         buff = store + l;
         bend = stend - l;
         aim(buff, bend , data, end, &j, &l);
         write_all(buff, j);
         write_all(data + l, end - l);
         break; }
      case 18:
      case 19: { /* insert semirandom bytes */
         size_t p = 0, n = RAND(BUFSIZE);
         size_t pos = (end ? random() % end : 0);
         n = RAND(n+1);
         n = RAND(n+1);
         n = RAND(n+1);
         n = RAND(n+1);
         n = (n > 1) ? n : 2;
         if (!end)
            goto retry;
         write_all(data, pos);
         LOGM("inssr");
         while(n--) {
            buff[p++] = data[RAND(end)];
         }
         write_all(buff, p);
         write_all(data + pos, end - pos);
         break; }
      case 20:
      case 21: { /* overwrite semirandom bytes */
         size_t a, b, p = 0;
         if (end < 2)
            goto retry;
         a = RAND(end-2);
         b = a + 2 + ((random() & 1) ? RAND(MIN(BUFSIZE-2, end-a-2)) : RAND(32));
         write_all(data, a);
         LOGM("oversr");
         while(a + p < b) {
            buff[p++] = data[RAND(end)];
         }
         write_all(buff, p);
         if (end > b)
            write_all(data + b, end - b);
         break; }
      case 22:
      case 23:
      case 24:
      case 25: { /* textual number mutation */
         int n = RAND(AIMROUNDS);
         long long val;
         size_t ns, ne;
         ns = ne = 0;
         if (!end) 
            goto retry;
         while(n-- && !ne) {
            seek_num(data, end, &ns, &ne);
         }
         if (!ne)
            goto retry;
         write_all(data, ns);
         LOGM("num");
         if (read_num(data + ns, ne - ns, &val) == 0) {
            output_num(buff, BUFSIZE, twiddle(val));
         } else {
            output_num(buff, BUFSIZE, twiddle(0));
         }
         write_all(data + ne, end - ne);
         break; }
      default:
         perror ("ni: bad mutation\n");
         exit(1);
   }
}

void ni_area(const char *data, size_t end, int n) {
   if (n == 0) {
      write_all(data, end);
      return;
   } else if (n == 1 || end == 0) {
      mutate_area(data, end);
   } else if (!end) {
      return;
   } else {
      size_t r = RAND(end);
      int m = RAND(n / 2);
      ni_area(data, r, (n - m));
      ni_area(data + r, end - r, m);
   }
}

int ni_file(char *path) {
   struct stat st;
   char *data;
   int fd = open(path, O_RDONLY);
   int m;
   if (verbose) fprintf(stderr, "= '%s'", path);
   if(fd < 0) {
      fprintf(stderr, "error opening '%s'", path);
      return 1;
   }
   if (stat(path, &st) < 0) {
      perror("error accessing file");
      return 1;
   }
   data = (st.st_size == 0) ? NULL : \
      mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

   if (data == MAP_FAILED) {
      fprintf(stderr, "error mapping input file '%s'\n", path);
      return 1;
   }
   m = ((random() & 3) == 1) ? 1 : \
       2 + RAND(((unsigned int) st.st_size >> 12) + 8);
   if (RAND(30)) {
      if (verbose) fprintf(stderr, ":");
      ni_area(data, st.st_size, m);
      munmap(data, st.st_size);
   } else { /* small chance of global tail flip */
      char *datap;
      size_t j, l, end, endp;
      int n = 0;
      m--;
      if (m) {
         n = RAND(m);
         m =- n;
      }
      end = st.st_size;
      path = samples[RAND(nsamples)];
      if (verbose) fprintf(stderr, " + '%s':", path);
      close(fd);
      fd = open(path, O_RDONLY);
      if (fd < 0) {
         fprintf(stderr, "Cannot open %s\n", path);
         return 1;
      }
      stat(path, &st);
      endp = st.st_size;
      datap = endp ? mmap(NULL, endp, PROT_READ, MAP_PRIVATE, fd, 0) : NULL;
      aim(data, end, datap, endp, &j, &l);
      ni_area(data, j, m);
      ni_area(datap + l, endp - l, n);
      munmap(data, end);
      munmap(datap, endp);
   }
   close(fd);
   if(verbose) fprintf(stderr, "\n");
   return 0;
}

int either(char **args, int pos, char *a, char *b) {
   return (strcmp(args[pos], a) == 0) || (strcmp(args[pos], b) == 0);
}

void print_usage() {
   printf("ni [-h] [-a] [-V] [--] <path> ...\n");
}

void badkitty(const char *why) {
   fprintf(stderr, "ni: %s\n", why);
   exit(1);
}

void process_args(int nargs, char **args) {
   int pos = 1;
   while(pos < nargs) {
      if (either(args, pos, "-h", "--help")) {
         print_usage();
         exit(0);
      } else if (either(args, pos, "-a", "--about")) {
         printf("ni: a small general purpose bit twiddler");
         exit(0);
      } else if (either(args, pos, "-V", "--version")) {
         printf("ni 0.1b\n");
         exit(0);
      } else if (either(args, pos, "-o", "--output")) {
         if (pos + 1 == nargs)
            badkitty("-o needs an argument");
         output_pattern = args[pos+1];
         pos += 2;
      } else if (either(args, pos, "-n", "--count")) {
         if (pos+1 == nargs)
            badkitty("-n needs an argument");
         count = atoi(args[pos+1]);
         pos += 2;
      } else if (either(args, pos, "-s", "--seed")) {
         if (pos+1 == nargs)
            badkitty("-s needs an arugment");
         seed = (unsigned int) atoi(args[pos+1]);
         srandom(seed);
         seeded = 1;
         pos += 2;
      } else if (either(args, pos, "-v", "--verbose")) {
         verbose = 1;
         pos++;
      } else if (either(args, pos, "--", "--stop-flags")) {
         pos++;
         samples = args + pos;
         nsamples = nargs - pos;
         break;
      } else {
         samples = args + pos;
         nsamples = nargs - pos;
         break;
      }
   }
}

void open_output(int nth) {
   if (output_pattern == NULL || strcmp(output_pattern, "-") == 0) {
      if(verbose) fprintf(stderr, "(stdout) ");
      output = 1;
   } else {
      char buff[4096];
      char *str;
      str = format_path((char *)&buff, 4096, output_pattern, nth);
      if(verbose) fprintf(stderr, "%s ", str);
      if (!str) {
         fprintf(stderr, "Failed to format path for testcase %d\n", nth);
         exit(1);
      }
      output = open(str, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
   }
}

int ni(int end) {
   int nth = 1;
   while(nth <= end) {
      open_output(nth);
      if (output < 0) {
         perror("could not start writing");
         exit(1);
      }
      if (ni_file(samples[RAND(nsamples)]))
         return 1;
      if (output > 2)
         close(output);
      nth++;
   }
   return 0;
}

int main(int nargs, char **args) {
   int fd = open("/dev/urandom", O_RDONLY);
   char *state = (char *) malloc(256);
   unsigned int seed = 0;
   output_pattern = NULL;
   seeded = verbose = 0;
   count = 1;
   if (fd < 0) {
      fprintf(stderr, "fail: cannot read /dev/urandom");
      exit(1);
   }
   process_args(nargs, args);
   if (nsamples == 0) {
      print_usage();
      exit(1);
   }
   output = 1; /* stdout */
   store = NULL;
   stend = 0;
   if (!seeded) {
      if (read(fd, state, 256) < 4) {
         perror("trouble reading random seed data");
         exit(1);
      }
      seed = state[0] | (state[1]<<8) | (state[2]<<16) | (state[3]<<24);
      initstate(seed, state, 256);
      setstate(state);
      seeded = 1;
   }
   return ni(count);
}

