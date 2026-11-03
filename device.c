/* Devices: where a buffer's memory lives. CpuDevice is a per-request singleton; ForeignDevice stands
 * for the device of an imported DLPack tensor that is not the CPU. */
#include "php_buffers.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "buffers_arginfo.h"

zend_class_entry *buf_ce_device_if, *buf_ce_cpudevice, *buf_ce_foreigndevice, *buf_ce_filedevice;
static zend_object_handlers device_handlers, foreign_device_handlers;

static zend_object *device_create_with(zend_class_entry *ce, zend_object_handlers *h, int32_t type, int32_t id)
{
	buf_device *d = zend_object_alloc(sizeof(buf_device), ce);

	zend_object_std_init(&d->std, ce);
	d->std.handlers = h;
	d->type = type;
	d->id = id;
	return &d->std;
}

static zend_object *cpudevice_create(zend_class_entry *ce) { return device_create_with(ce, &device_handlers, kDLCPU, 0); }
static zend_object *filedevice_create(zend_class_entry *ce) { return device_create_with(ce, &device_handlers, BUF_DEV_FILE, 0); }
static zend_object *foreigndevice_create(zend_class_entry *ce) { return device_create_with(ce, &foreign_device_handlers, 0, 0); }

zend_object *buf_foreign_device_new(int32_t type, int32_t id)
{
	return device_create_with(buf_ce_foreigndevice, &foreign_device_handlers, type, id);
}

static zend_function *foreign_no_constructor(zend_object *o)
{
	zend_throw_error(NULL, "ForeignDevice cannot be instantiated; it comes from the DLPack import of the C API");
	return NULL;
}

const char *buf_device_type_name(int32_t type)
{
	switch (type) {
	case BUF_DEV_FILE: return "file";
	case kDLCPU: return "cpu";
	case kDLCUDA: return "cuda";
	case kDLCUDAHost: return "cuda_host";
	case kDLOpenCL: return "opencl";
	case kDLVulkan: return "vulkan";
	case kDLMetal: return "metal";
	case kDLVPI: return "vpi";
	case kDLROCM: return "rocm";
	case kDLROCMHost: return "rocm_host";
	case kDLExtDev: return "ext_dev";
	case kDLCUDAManaged: return "cuda_managed";
	case kDLOneAPI: return "oneapi";
	case kDLWebGPU: return "webgpu";
	case kDLHexagon: return "hexagon";
	case kDLMAIA: return "maia";
	case kDLTrn: return "trn";
	case kDLTPU: return "tpu";
	case kDLTPUHost: return "tpu_host";
	case kDLAscend: return "ascend";
	}
	return NULL;
}

/* Host-accessible device types: the CPU and the host-side memory of accelerators. Such memory can
 * still have pending work: that is the readiness hook's business. */
static bool host_accessible(int32_t type)
{
	return type == kDLCPU || type == kDLCUDAHost || type == kDLROCMHost || type == kDLCUDAManaged;
}

/* "cpu", "cuda:0", "device99:7" */
zend_string *buf_device_name(int32_t type, int32_t id)
{
	const char *n = buf_device_type_name(type);

	if (type == kDLCPU) return zend_string_init("cpu", 3, 0);
	if (type == BUF_DEV_FILE) return zend_string_init("file", 4, 0);
	if (type == BUF_DEV_OTHER) return zend_string_init("device", 6, 0);
	if (n) return zend_strpprintf(0, "%s:%d", n, (int)id);
	return zend_strpprintf(0, "device%d:%d", (int)type, (int)id);
}

/* the DLPack device types of the Device classes of other extensions */
#define MAX_DEVICE_TYPES 16
static struct { zend_class_entry *ce; int type; } device_types[MAX_DEVICE_TYPES];
static int ndevice_types;

zend_result buffers_register_device_type(zend_class_entry *device_ce, int dlpack_type)
{
	if (!device_ce || dlpack_type < 1 || ndevice_types == MAX_DEVICE_TYPES) {
		return FAILURE;
	}
	for (int i = 0; i < ndevice_types; i++) {
		if (device_types[i].ce == device_ce) return FAILURE;
	}
	device_types[ndevice_types].ce = device_ce;
	device_types[ndevice_types++].type = dlpack_type;
	return SUCCESS;
}

zend_result buffers_device_dlpack(zend_object *device, int *type, int *id)
{
	int32_t t, i;
	bool host;

	if (!buf_device_query(device, &t, &i, &host)) {
		return FAILURE;
	}
	if (t < 1) {
		zend_clear_exception();
		return FAILURE;
	}
	*type = t;
	*id = i;
	return SUCCESS;
}

bool buf_device_query(zend_object *dev, int32_t *type, int32_t *id, bool *host)
{
	zval rv;

	if (!dev || dev->ce == buf_ce_cpudevice || dev->ce == buf_ce_foreigndevice || dev->ce == buf_ce_filedevice) {
		buf_device *d = dev ? buf_device_from(dev) : NULL;

		*type = d ? d->type : kDLCPU;
		*id = d ? d->id : 0;
		*host = host_accessible(*type);
		return true;
	}
	/* a Device implemented by another extension */
	*type = BUF_DEV_OTHER;
	for (int i = 0; i < ndevice_types; i++) {
		if (instanceof_function(dev->ce, device_types[i].ce)) {
			*type = device_types[i].type;
			break;
		}
	}
	{
		zval *p = zend_read_property(dev->ce, dev, "id", sizeof("id") - 1, 1, &rv);

		if (EG(exception) || Z_TYPE_P(p) != IS_LONG) goto fail;
		*id = (int32_t)Z_LVAL_P(p);
		p = zend_read_property(dev->ce, dev, "hostAccessible", sizeof("hostAccessible") - 1, 1, &rv);
		if (EG(exception) || (Z_TYPE_P(p) != IS_TRUE && Z_TYPE_P(p) != IS_FALSE)) goto fail;
		*host = Z_TYPE_P(p) == IS_TRUE;
	}
	return true;
fail:
	if (!EG(exception)) {
		zend_throw_error(NULL, "%s::$id and ::$hostAccessible must be an int and a bool", ZSTR_VAL(dev->ce->name));
	}
	return false;
}

#define SELF buf_device_from(Z_OBJ_P(ZEND_THIS))

PHP_METHOD(Buffers_CpuDevice, __construct) { ZEND_PARSE_PARAMETERS_NONE(); }

PHP_METHOD(Buffers_CpuDevice, get)
{
	ZEND_PARSE_PARAMETERS_NONE();
	RETURN_OBJ_COPY(buffers_cpu_device());
}

zend_object *buffers_cpu_device(void)
{
	if (!BUFG(cpu_device)) {
		BUFG(cpu_device) = cpudevice_create(buf_ce_cpudevice);
	}
	return BUFG(cpu_device);
}

void buf_device_rshutdown(void)
{
	if (BUFG(file_device)) {
		OBJ_RELEASE(BUFG(file_device));
		BUFG(file_device) = NULL;
	}
	if (BUFG(cpu_device)) {
		OBJ_RELEASE(BUFG(cpu_device));
		BUFG(cpu_device) = NULL;
	}
}

PHP_METHOD(Buffers_FileDevice, __construct) { ZEND_PARSE_PARAMETERS_NONE(); }
PHP_METHOD(Buffers_FileDevice, get) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_OBJ_COPY(buf_file_device()); }

zend_object *buf_file_device(void)
{
	if (!BUFG(file_device)) {
		BUFG(file_device) = filedevice_create(buf_ce_filedevice);
	}
	return BUFG(file_device);
}

/* The facts of a device are read-only virtual properties (internal get hooks, see props.c): $name, $id, $hostAccessible.
 * One set of hooks serves CpuDevice, FileDevice and ForeignDevice: they all carry a DLPack-style type and id. */
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_dev_name, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_dev_id, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_dev_host, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

static ZEND_NAMED_FUNCTION(hook_device_name)
{
	buf_device *d = SELF;

	ZEND_PARSE_PARAMETERS_NONE();
	RETURN_STR(buf_device_name(d->type, d->id));
}
static ZEND_NAMED_FUNCTION(hook_device_id) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG(SELF->id); }
static ZEND_NAMED_FUNCTION(hook_device_host) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_BOOL(host_accessible(SELF->type)); }

static void declare_device_props(zend_class_entry *ce, zif_handler name, zif_handler id, zif_handler host)
{
	const buf_prop_def defs[] = {
		{ "name", (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_STRING), name, ai_dev_name, 0 },
		{ "id", (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_LONG), id, ai_dev_id, 0 },
		{ "hostAccessible", (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_BOOL), host, ai_dev_host, 0 },
	};

	buf_declare_hooked_props(ce, defs, 3);
}

void buffers_implement_device(zend_class_entry *ce, zif_handler get_name, zif_handler get_id, zif_handler get_host_accessible)
{
	declare_device_props(ce, get_name, get_id, get_host_accessible);
	zend_class_implements(ce, 1, buf_ce_device_if);
}

static void init_handlers(zend_object_handlers *h)
{
	memcpy(h, &std_object_handlers, sizeof *h);
	h->offset = XtOffsetOf(buf_device, std);
	h->clone_obj = NULL;
	h->compare = buf_compare_identity;
}

void buf_device_minit(void)
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Buffers\\Device", NULL);
	buf_ce_device_if = zend_register_internal_interface(&ce);
	declare_device_props(buf_ce_device_if, NULL, NULL, NULL);

	INIT_CLASS_ENTRY(ce, "Buffers\\CpuDevice", class_Buffers_CpuDevice_methods);
	buf_ce_cpudevice = zend_register_internal_class_ex(&ce, NULL);
	buf_ce_cpudevice->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	buf_ce_cpudevice->create_object = cpudevice_create;
	declare_device_props(buf_ce_cpudevice, hook_device_name, hook_device_id, hook_device_host);
	zend_class_implements(buf_ce_cpudevice, 1, buf_ce_device_if);
	init_handlers(&device_handlers);

	INIT_CLASS_ENTRY(ce, "Buffers\\FileDevice", class_Buffers_FileDevice_methods);
	buf_ce_filedevice = zend_register_internal_class_ex(&ce, NULL);
	buf_ce_filedevice->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	buf_ce_filedevice->create_object = filedevice_create;
	declare_device_props(buf_ce_filedevice, hook_device_name, hook_device_id, hook_device_host);
	zend_class_implements(buf_ce_filedevice, 1, buf_ce_device_if);

	INIT_CLASS_ENTRY(ce, "Buffers\\ForeignDevice", NULL);
	buf_ce_foreigndevice = zend_register_internal_class_ex(&ce, NULL);
	buf_ce_foreigndevice->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	buf_ce_foreigndevice->create_object = foreigndevice_create;
	declare_device_props(buf_ce_foreigndevice, hook_device_name, hook_device_id, hook_device_host);
	zend_class_implements(buf_ce_foreigndevice, 1, buf_ce_device_if);
	init_handlers(&foreign_device_handlers);
	foreign_device_handlers.get_constructor = foreign_no_constructor;
}
