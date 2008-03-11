
/* $Id$ */

#include "main.h"

/* So that the usage message fits in a standard DOS window, this
   function should produce no more than 22 lines of text. */
static void usage(void) 
{
  print_status("%s version %s by %s.",__progname,VERSION,AUTHOR);
  /* print_status("%s %s [OPTION]... [FILES]...",CMD_PROMPT,__progname); */
  print_status("%s %s [-c <alg>] [-k <file>] [-amxwMXnrespblvv] [-V|-h] ... [FILES]...",CMD_PROMPT,__progname);

  print_status("");

  print_status("-c <alg1,[alg2]> - Compute hashes only. Defaults are MD5 and SHA-1");
  print_status("         available values are md5,sha1,sha256,tiger,whirlpool");

  print_status("-a - Audit mode. Validates FILES against known hashes. Requires -k");
  print_status("-m - Matching mode. Requires -k");
  print_status("-x - Negative matching mode. Requires -k");
  print_status("-w - In -m mode, displays which known file was matched");
  print_status("-M and -X act like -m and -x, but display hashes of matching files");
  print_status("-k - Add a file of known hashes");

  print_status("-r - recursive mode. All subdirectories are traversed");
  print_status("-e - compute estimated time remaining for each file");
  print_status("-s - silent mode. Suppress all error messages");
  print_status("-p - piecewise mode. Files are broken into blocks for hashing");
  print_status("-b - prints only the bare name of files; all path information is omitted");
  print_status("-l - print relative paths for filenames");
  print_status("-v - Verbose mode. Use again to be more verbose.");
  print_status("-V - display version number and exit");
}


static void check_flags_okay(state *s)
{
  sanity_check(s,
	       (((s->primary_function & primary_match) ||
		 (s->primary_function & primary_match_neg) ||
		 (s->primary_function & primary_audit)) &&
		!s->hashes_loaded),
	       "Unable to load any matching files");

  sanity_check(s,
	       (s->mode & mode_relative) && (s->mode & mode_barename),
	       "Relative paths and bare filenames are mutally exclusive");
  
  /* Additional sanity checks will go here as needed... */
}



static int 
add_algorithm(state *s, 
	      hashname_t pos,
	      char *name, 
	      uint16_t len, 
	      int ( *func_init)(void *),
	      int ( *func_update)(void *, unsigned char *, uint64_t ),
	      int ( *func_finalize)(void *, unsigned char *),
	      int inuse)
{
  if (NULL == s)
    return TRUE;

  /*
  printf ("Setting up %s in slot %d %"PRIu16" bytes (%s)\n", 
	  name, pos, len, inuse?"ON":"OFF");
  */

  s->hashes[pos] = (algorithm_t *)malloc(sizeof(algorithm_t));
  if (NULL == s->hashes[pos])
    return TRUE;

  s->hashes[pos]->name = strdup(name);
  if (NULL == s->hashes[pos]->name)
    return TRUE;

  s->hashes[pos]->known = (hashtable_t *)malloc(sizeof(hashtable_t));
  if (NULL == s->hashes[pos]->known)
    return TRUE;

  hashtable_init(s->hashes[pos]->known); 

  /* We always store the result in a file_data_t structure
  s->hashes[pos]->result = (unsigned char *)malloc(len);
  if (NULL == s->hashes[pos]->result)
    return TRUE;
  */

  s->hashes[pos]->hash_sum = (unsigned char *)malloc(len * 2);
  if (NULL == s->hashes[pos]->hash_sum)
    return TRUE;

  s->hashes[pos]->hash_context = malloc(ALGORITHM_CONTEXT_SIZE);
  if (NULL == s->hashes[pos]->hash_context)
    return TRUE;
  
  s->hashes[pos]->position    = pos;
  s->hashes[pos]->f_init      = func_init;
  s->hashes[pos]->f_update    = func_update;
  s->hashes[pos]->f_finalize  = func_finalize;
  s->hashes[pos]->byte_length = len;
  s->hashes[pos]->inuse       = inuse;
  s->hashes[pos]->howmany     = 0;

  return FALSE;
}


int setup_hashing_algorithms(state *s)
{
  if (NULL == s)
    return TRUE;

  /* Only MD5 and SHA-256 are enabled by default */

  if (add_algorithm(s,
		    alg_md5,
		    "md5",
		    32,
		    hash_init_md5,
		    hash_update_md5,
		    hash_final_md5,
		    TRUE))
    return TRUE;
  if (add_algorithm(s,
		    alg_sha1,
		    "sha1",
		    40,
		    hash_init_sha1,
		    hash_update_sha1,
		    hash_final_sha1,
		    FALSE))
    return TRUE;
  if (add_algorithm(s,
		    alg_sha256,
		    "sha256",
		    64,
		    hash_init_sha256,
		    hash_update_sha256,
		    hash_final_sha256,
		    TRUE))
    return TRUE;
  if (add_algorithm(s,
		    alg_tiger,
		    "tiger",
		    48,
		    hash_init_tiger,
		    hash_update_tiger,
		    hash_final_tiger,
		    FALSE))
    return TRUE;
  if (add_algorithm(s,
		    alg_whirlpool,
		    "whirlpool",
		    128,
		    hash_init_whirlpool,
		    hash_update_whirlpool, 
		    hash_final_whirlpool,
		    FALSE))
    return TRUE;

  return FALSE;
}


void clear_algorithms_inuse(state *s)
{
  hashname_t i;

  if (NULL == s)
    return;

  for (i = 0 ; i < NUM_ALGORITHMS ; ++i)
  { 
    if (s->hashes[i] != NULL)
      s->hashes[i]->inuse = FALSE;
  }
}


static int initialize_hashing_algorithms(state *s)
{
  hashname_t i;
  
  for (i = 0 ; i < NUM_ALGORITHMS ; ++i)
  {
    if (NULL == s->current_file->hash[i])
    {
      s->current_file->hash[i] = (char *)malloc(sizeof(char) * s->hashes[i]->byte_length * 2);
      if (NULL == s->current_file->hash[i])
	fatal_error(s,"%s: Out of memory", __progname);
    }
  }

  return FALSE;
}


static int parse_hashing_algorithms(state *s, char *val)
{
  uint8_t i;
  char **ap, *buf[MAX_KNOWN_COLUMNS];

  if (NULL == s || NULL == val)
    return TRUE;

  for (ap = buf ; (*ap = strsep(&val,",")) != NULL ; )
    if (*ap != '\0')
      if (++ap >= &buf[MAX_KNOWN_COLUMNS])
	break;

  for (i = 0 ; i < MAX_KNOWN_COLUMNS && buf[i] != NULL ; i++)
  {
    if (STRINGS_EQUAL(buf[i],"md5"))
      s->hashes[alg_md5]->inuse = TRUE;
    
    else if (STRINGS_EQUAL(buf[i],"sha1") || 
	     STRINGS_EQUAL(buf[i],"sha-1"))
      s->hashes[alg_sha1]->inuse = TRUE;
    
    else if (STRINGS_EQUAL(buf[i],"sha256") || 
	     STRINGS_EQUAL(buf[i],"sha-256"))
      s->hashes[alg_sha256]->inuse = TRUE;
    
    else if (STRINGS_EQUAL(buf[i],"tiger"))
      s->hashes[alg_tiger]->inuse = TRUE;
    
    else if (STRINGS_EQUAL(buf[i],"whirlpool"))
      s->hashes[alg_whirlpool]->inuse = TRUE;
    
    else if (STRINGS_EQUAL(buf[i],"all"))
    {
      hashname_t count;
      for (count = 0 ; count < NUM_ALGORITHMS ; ++count)
	s->hashes[count]->inuse = TRUE;
      return FALSE;
    }
      
    else
    {
      print_error(s,"%s: %s: Unknown algorithm", __progname, buf);
      try_msg();
      exit(EXIT_FAILURE);
    }
    
  }
    
  return FALSE;
}

static int process_command_line(state *s, int argc, char **argv)
{
  int i;
  
  while ((i=getopt(argc,argv,"c:MmXxablk:respwvVh")) != -1)
    {
      switch (i)
	{
	  
	case 'c': 
	  s->primary_function = primary_compute;
	  /* Before we parse which algorithms we're using now, we have 
	     to erase the default values */
	  clear_algorithms_inuse(s);
	  if (parse_hashing_algorithms(s,optarg))
	    fatal_error(s,"%s: Unable to parse hashing algorithms",__progname);
	  break;

	case 'M': s->mode |= mode_display_hash;	  
	case 'm': s->primary_function = primary_match;      break;

	case 'X': s->mode |= mode_display_hash;
	case 'x': s->primary_function = primary_match_neg;  break;

	case 'a': s->primary_function = primary_audit;      break;
	  
	case 'b': s->mode |= mode_barename;     break;
	case 'l': s->mode |= mode_relative;     break;
	case 'e': s->mode |= mode_estimate;     break;
	case 'r': s->mode |= mode_recursive;    break;
	case 's': s->mode |= mode_silent;       break;
	case 'p': s->mode |= mode_piecewise;    break;
	case 'w': s->mode |= mode_which;        break;
	  
	case 'k':
	  switch (load_match_file(s,optarg))
	  {
	  case status_ok: 
	    s->hashes_loaded = TRUE;
	    break;

	  case status_contains_no_hashes:
	    /* Trying to load an empty file is fine, but we shouldn't
	       change s->hashes_loaded */
	    break;

	  case status_contains_bad_hashes:
	    s->hashes_loaded = TRUE;
	    print_error(s,"%s: %s: contains some bad hashes, using anyway", __progname, optarg);
	    break;

	  case status_unknown_filetype:
	    print_error(s,"%s: %s: unknown filetype, skipping", __progname, optarg);
	    break;

	  case status_file_error:
	    /* The loading code has already printed an error */
	    break;

	  default:
	    print_error(s,"%s: %s: unknown error, skipping", __progname, optarg);
	    break;
	  }
	  break;

	case 'v':
	  if (s->mode & mode_verbose)
	    s->mode |= mode_more_verbose;
	  else
	    s->mode |= mode_verbose;
	  break;

	case 'V':
	  print_status("%s", VERSION);
	  exit(EXIT_SUCCESS);
	  
	case 'h':
	  usage();
	  exit(EXIT_SUCCESS);
	  
	default:
	  try_msg();
	  exit(EXIT_FAILURE);
	}            
    }

  check_flags_okay(s);

  return FALSE;
}

static int initialize_state(state *s) 
{
  if (NULL == s)
    return TRUE;

  if (setup_hashing_algorithms(s))
    return TRUE;

  MD5DEEP_ALLOC(file_data_t,s->current_file,1);
  MD5DEEP_ALLOC(TCHAR,s->full_name,PATH_MAX);
  MD5DEEP_ALLOC(TCHAR,s->short_name,PATH_MAX);
  MD5DEEP_ALLOC(TCHAR,s->msg,PATH_MAX);
  MD5DEEP_ALLOC(unsigned char,s->buffer,MD5DEEP_IDEAL_BLOCK_SIZE);

  s->known            = NULL;
  s->last             = NULL;
  s->piecewise_size   = 0;
  s->hash_round       = 0;
  s->primary_function = primary_compute;
  s->mode             = mode_none;
  s->hashes_loaded    = FALSE;
  s->next_known_id    = 0;
  s->banner_displayed = FALSE;
  s->block_size       = MD5DEEP_IDEAL_BLOCK_SIZE;

  return FALSE;
}


#ifdef _WIN32
static int prepare_windows_command_line(state *s)
{
  int argc;
  TCHAR **argv;

  argv = CommandLineToArgvW(GetCommandLineW(),&argc);
  
  s->argc = argc;
  s->argv = argv;

  return FALSE;
}
#endif


int main(int argc, char **argv)
{
  int count, status = EXIT_SUCCESS;
  TCHAR *fn;
  state *s;

  /* Because the main() function can handle wchar_t arguments on Win32,
     we need a way to reference those values. Thus we make a duplciate
     of the argc and argv values. */ 

#ifndef __GLIBC__
  __progname  = basename(argv[0]);
#endif

  s = (state *)malloc(sizeof(state));
  if (NULL == s)
  {
    // We can't use fatal_error because it requires a valid state
    print_status("%s: Unable to allocate state variable", __progname);
    return EXIT_FAILURE;
  }

  if (initialize_state(s))
  {
    print_status("%s: Unable to initialize state variable", __progname);
    return EXIT_FAILURE;
  }

  process_command_line(s,argc,argv);
 
  if (initialize_hashing_algorithms(s))
    return EXIT_FAILURE;
   
  if (primary_audit == s->primary_function)
    setup_audit(s);

#ifdef _WIN32
  if (prepare_windows_command_line(s))
    fatal_error(s,"%s: Unable to process command line arguments", __progname);
#else
  s->argc = argc;
  s->argv = argv;
#endif

  MD5DEEP_ALLOC(TCHAR,s->cwd,PATH_MAX);
  s->cwd = _tgetcwd(s->cwd,PATH_MAX);
  if (NULL == s->cwd)
    fatal_error(s,"%s: %s", __progname, strerror(errno));

  /* Anything left on the command line at this point is a file
     or directory we're supposed to process. If there's nothing
     specified, we should tackle standard input */
  
  if (optind == argc)
    hash_stdin(s);
  else
  {
    MD5DEEP_ALLOC(TCHAR,fn,PATH_MAX);

    count = optind;

    while (count < s->argc)
    {  
      generate_filename(s,fn,s->cwd,s->argv[count]);

#ifdef _WIN32
      status = process_win32(s,fn);
#else
      status = process_normal(s,fn);
#endif

     ++count;
    }

    free(fn);
  }
  
  if (primary_audit == s->primary_function)
    display_audit_results(s);

  return status;
}









