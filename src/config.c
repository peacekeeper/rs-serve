/*
 * rs-serve - (c) 2013 Niklas E. Cathor
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "rs-serve.h"

static void print_help(const char *progname) {
  fprintf(stderr,
          "Usage: %s [options]\n"
          "\n"
          "Options:\n"
          "  -h        | --help            - Display this text and exit.\n"
          "  -v        | --version         - Print program version and exit.\n"
          "  -p <port> | --port=<port>     - Bind to given port (default: 80).\n"
          "  -n <name> | --hostname=<name> - Set hostname (defaults to local.dev).\n"
          "  -r <root> | --root=<root>     - Root directory to serve (defaults to cwd).\n"
          "              --chroot          - chroot() to root directory before serving any\n"
          "                                  files.\n"
          "  -f <file> | --log-file=<file> - Log to given file (defaults to stdout)\n"
          "  -d        | --detach          - After starting the server, detach server\n"
          "                                  process and exit. If you don't use this in\n"
          "                                  combination with the --log-file option, all\n"
          "                                  future output will be lost.\n"
          "  -u        | --uid             - After binding to the specified port (but\n"
          "                                  before accepting any connections) set the\n"
          "                                  user ID of to the given value. This only\n"
          "                                  works if the process is run as root.\n"
          "              --user            - Same as --uid option, but specify the user\n"
          "                                  by name.\n"
          "  -g        | --gid             - Same as --uid, but setting the group ID.\n"
          "              --group           - Same as --user, but setting the group.\n"
          "\n"
          "This program is distributed in the hope that it will be useful,\n"
          "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
          "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
          "GNU Affero General Public License for more details.\n"
          "(c) 2013 Niklas E. Cathor\n\n"
          , progname);
}

static void print_version() {
  fprintf(stderr, "rs-serve %d.%d%s\n", RS_VERSION_MAJOR, RS_VERSION_MINOR, RS_VERSION_POSTFIX);
}

int rs_port = 80;
char *rs_hostname = "local.dev";
char *rs_storage_root = NULL;
int rs_storage_root_len = 0;
int rs_chroot = 0;
int rs_detach = 0;
// equivalents of rs_storage_root(_len), but rs_chroot aware
char *rs_real_storage_root = NULL;
int rs_real_storage_root_len = 0;
FILE *rs_log_file = NULL;
uid_t rs_set_uid = 0;
gid_t rs_set_gid = 0;

static struct option long_options[] = {
  { "port", required_argument, 0, 'p' },
  { "hostname", required_argument, 0, 'n' },
  { "root", required_argument, 0, 'r' },
  { "chroot", no_argument, 0, 0 },
  { "uid", required_argument, 0, 'u' },
  { "gid", required_argument, 0, 'g' },
  { "user", required_argument, 0, 0 },
  { "group", required_argument, 0, 0 },
  // TODO:
  //{ "listen", required_argument, 0, 'l' },
  { "log-file", required_argument, 0, 'f' },
  { "detach", no_argument, 0, 'd' },
  { "help", no_argument, 0, 'h' },
  { "version", no_argument, 0, 'v' },
  { 0, 0, 0, 0 }
};

void init_config(int argc, char **argv) {
  int opt;
  for(;;) {
    int opt_index = 0;
    opt = getopt_long(argc, argv, "p:n:r:f:du:g:hv", long_options, &opt_index);
    if(opt == '?') {
      // invalid option
      exit(EXIT_FAILURE);
    } else if(opt == -1) {
      // no more options
      break;
    } else if(opt == 'p') {
      rs_port = atoi(optarg);
    } else if(opt == 'n') {
      rs_hostname = optarg;
    } else if(opt == 'r') {
      rs_storage_root = strdup(optarg);
      rs_storage_root_len = strlen(rs_storage_root);
    } else if(opt == 'f') {
      rs_log_file = fopen(optarg, "a");
      if(rs_log_file == NULL) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
      }
    } else if(opt == 'd') {
      rs_detach = 1;
    } else if(opt == 'u') {
      rs_set_uid = atoi(optarg);
    } else if(opt == 'g') {
      rs_set_gid = atoi(optarg);
    } else if(opt == 'h') {
      print_help(argv[0]);
      exit(127);
    } else if(opt == 'v') {
      print_version();
      exit(127);
    } else if(opt == 0) {
      // long option with no short equivalent
      if(strcmp(long_options[opt_index].name, "chroot") == 0) {
        rs_chroot = 1;
      } else if(strcmp(long_options[opt_index].name, "user") == 0) {
        errno = 0;
        struct passwd *user_entry = getpwnam(optarg);
        if(user_entry == NULL) {
          if(errno != 0) {
            perror("getpwnam() failed");
          } else {
            fprintf(stderr, "Failed to find UID for user \"%s\".\n", optarg);
          }
          exit(EXIT_FAILURE);
        }
        rs_set_uid = user_entry->pw_uid;
      } else if(strcmp(long_options[opt_index].name, "group") == 0) {
        errno = 0;
        struct group *group_entry = getgrnam(optarg);
        if(group_entry == NULL) {
          if(errno != 0) {
            perror("getgrnam() failed");
          } else {
            fprintf(stderr, "Failed to find GID for group \"%s\".\n", optarg);
          }
          exit(EXIT_FAILURE);
        }
        rs_set_gid = group_entry->gr_gid;
      }
    }
  }

  // init RS_STORAGE_ROOT
  if(rs_storage_root == NULL) {
    rs_storage_root = malloc(PATH_MAX + 1);
    if(getcwd(rs_storage_root, PATH_MAX + 1) == NULL) {
      perror("getcwd() failed");
      exit(EXIT_FAILURE);
    }
    rs_storage_root_len = strlen(rs_storage_root);
    rs_storage_root = realloc(rs_storage_root, rs_storage_root_len + 1);
  }

  if(RS_CHROOT) {
    rs_real_storage_root = "";
    rs_real_storage_root_len = 0;
  } else {
    rs_real_storage_root = rs_storage_root;
    rs_real_storage_root_len = rs_storage_root_len;
  }

  if(rs_log_file == NULL) {
    rs_log_file = stdout;
  }

}

void cleanup_config() {
  free(rs_storage_root);
}
