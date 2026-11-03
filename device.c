/* Devices: where a buffer's memory lives. CpuDevice is a per-request singleton; ForeignDevice stands
 * for the device of an imported DLPack tensor that is not the CPU. */
#include "php_buffers.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "buffers_arginfo.h"

zend_class_entry *buf_ce_device_if, *buf_ce_cpudevice, *buf_ce_foreigndevice;
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
static zend_object *foreigndevice_create(zend_class_entry *ce) { return device_create_with(ce, &foreign_device_handlers, 0, 0); }

zend_object *buf_foreign_device_new(int32_t type, int32_t id)
{
	return device_create_with(buf_ce_foreigndevice, &foreign_device_handlers, type, id);
}

static zend_function *foreign_no_constructor(zend_object *o)
{
	zend_throw_error(NULL, "ForeignDevice cannot be instantiated; it comes from BufferView::fromDLPack()");
	return NULL;
}

const char *buf_device_type_name(int32_t type)
{
	switch (type) {
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

bool buf_device_query(zend_object *dev, int32_t *type, int32_t *id, bool *host)
{
	zval rv;

	if (!dev || dev->ce == buf_ce_cpudevice || dev->ce == buf_ce_foreigndevice) {
		buf_device *d = dev ? buf_device_from(dev) : NULL;

		*type = d ? d->type : kDLCPU;
		*id = d ? d->id : 0;
		*host = host_accessible(*type);
		return true;
	}
	/* a Device implemented by another extension */
	zend_call_method_with_0_params(dev, dev->ce, NULL, "dlpackType", &rv);
	if (EG(exception) || Z_TYPE(rv) != IS_LONG) goto fail;
	*type = (int32_t)Z_LVAL(rv);
	zend_call_method_with_0_params(dev, dev->ce, NULL, "id", &rv);
	if (EG(exception) || Z_TYPE(rv) != IS_LONG) goto fail;
	*id = (int32_t)Z_LVAL(rv);
	zend_call_method_with_0_params(dev, dev->ce, NULL, "isHostAccessible", &rv);
	if (EG(exception) || (Z_TYPE(rv) != IS_TRUE && Z_TYPE(rv) != IS_FALSE)) goto fail;
	*host = Z_TYPE(rv) == IS_TRUE;
	return true;
fail:
	if (!EG(exception)) {
		zend_throw_error(NULL, "%s::dlpackType(), id() and isHostAccessible() must return int, int and bool", ZSTR_VAL(dev->ce->name));
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
	if (BUFG(cpu_device)) {
		OBJ_RELEASE(BUFG(cpu_device));
		BUFG(cpu_device) = NULL;
	}
}

#define DEVICE_METHODS(cls) \
	PHP_METHOD(cls, name) { \
		buf_device *d = SELF; const char *n = buf_device_type_name(d->type); \
		ZEND_PARSE_PARAMETERS_NONE(); \
		if (d->type == kDLCPU) RETURN_STRING("cpu"); \
		if (n) RETURN_NEW_STR(zend_strpprintf(0, "%s:%d", n, (int)d->id)); \
		RETURN_NEW_STR(zend_strpprintf(0, "device%d:%d", (int)d->type, (int)d->id)); } \
	PHP_METHOD(cls, dlpackType) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG(SELF->type); } \
	PHP_METHOD(cls, id) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_LONG(SELF->id); } \
	PHP_METHOD(cls, isHostAccessible) { ZEND_PARSE_PARAMETERS_NONE(); RETURN_BOOL(host_accessible(SELF->type)); }
DEVICE_METHODS(Buffers_CpuDevice)
DEVICE_METHODS(Buffers_ForeignDevice)

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

	INIT_CLASS_ENTRY(ce, "Buffers\\Device", class_Buffers_Device_methods);
	buf_ce_device_if = zend_register_internal_interface(&ce);

	INIT_CLASS_ENTRY(ce, "Buffers\\CpuDevice", class_Buffers_CpuDevice_methods);
	buf_ce_cpudevice = zend_register_internal_class_ex(&ce, NULL);
	buf_ce_cpudevice->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	buf_ce_cpudevice->create_object = cpudevice_create;
	zend_class_implements(buf_ce_cpudevice, 1, buf_ce_device_if);
	init_handlers(&device_handlers);

	INIT_CLASS_ENTRY(ce, "Buffers\\ForeignDevice", class_Buffers_ForeignDevice_methods);
	buf_ce_foreigndevice = zend_register_internal_class_ex(&ce, NULL);
	buf_ce_foreigndevice->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES | ZEND_ACC_NOT_SERIALIZABLE;
	buf_ce_foreigndevice->create_object = foreigndevice_create;
	zend_class_implements(buf_ce_foreigndevice, 1, buf_ce_device_if);
	init_handlers(&foreign_device_handlers);
	foreign_device_handlers.get_constructor = foreign_no_constructor;
}
