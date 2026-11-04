/* SPDX-License-Identifier: MIT */
/* Settings-only workaround for Core Image failing to create its GL context. */
#include <objc/runtime.h>
#include <objc/message.h>
#include <CoreGraphics/CGGeometry.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static id
preferences_icon_passthrough(id image, SEL selector, long format, signed char precomposed,
    long idiom, double scale)
{
	void *(*get_image)(id, SEL) = (void *(*)(id, SEL))objc_msgSend;
	long (*get_orientation)(id, SEL) = (long (*)(id, SEL))objc_msgSend;
	id (*make_image)(id, SEL, void *, double, long) = (id (*)(id, SEL, void *, double, long))objc_msgSend;
	size_t (*width)(void *) = (size_t (*)(void *))dlsym(RTLD_DEFAULT, "CGImageGetWidth");
	size_t (*height)(void *) = (size_t (*)(void *))dlsym(RTLD_DEFAULT, "CGImageGetHeight");
	void *cg_image;
	id sized;
	id renderer, path, rounded;
	Class renderer_class, path_class;
	size_t pixels;

	(void)selector;
	(void)format;
	(void)precomposed;
	(void)idiom;
	(void)scale;
	if (width == NULL || height == NULL || image == nil) {
		return image;
	}
	cg_image = get_image(image, sel_registerName("CGImage"));
	if (cg_image == NULL || (pixels = width(cg_image)) == 0 || pixels != height(cg_image)) {
		return image;
	}
	/* Preserve the original pixels and use the documented 29-point Settings size. */
	sized = make_image((id)objc_getClass("UIImage"), sel_registerName("imageWithCGImage:scale:orientation:"),
	    cg_image, (double)pixels / 29.0, get_orientation(image, sel_registerName("imageOrientation")));
	if (sized == nil) {
		return image;
	}
	renderer_class = objc_getClass("UIGraphicsImageRenderer");
	path_class = objc_getClass("UIBezierPath");
	if (renderer_class == Nil || path_class == Nil) {
		return sized;
	}
	CGRect rect = {{0, 0}, {29, 29}};
	id (*send0)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
	id (*init_size)(id, SEL, CGSize) = (id (*)(id, SEL, CGSize))objc_msgSend;
	id (*rounded_path)(id, SEL, CGRect, double) = (id (*)(id, SEL, CGRect, double))objc_msgSend;
	id (*render)(id, SEL, void (^)(id)) = (id (*)(id, SEL, void (^)(id)))objc_msgSend;
	void (*send_void)(id, SEL) = (void (*)(id, SEL))objc_msgSend;
	void (*draw)(id, SEL, CGRect) = (void (*)(id, SEL, CGRect))objc_msgSend;
	renderer = init_size(send0((id)renderer_class, sel_registerName("alloc")), sel_registerName("initWithSize:"), rect.size);
	path = rounded_path((id)path_class, sel_registerName("bezierPathWithRoundedRect:cornerRadius:"), rect, 6.5);
	if (renderer == nil || path == nil) {
		if (renderer != nil) send_void(renderer, sel_registerName("release"));
		return sized;
	}
	rounded = render(renderer, sel_registerName("imageWithActions:"), ^(id context) {
		(void)context;
		send_void(path, sel_registerName("addClip"));
		draw(sized, sel_registerName("drawInRect:"), rect);
	});
	send_void(renderer, sel_registerName("release"));
	return rounded != nil ? rounded : sized;
}

__attribute__((constructor)) static void
preferences_icon_fix_init(void)
{
	const char *name = getprogname();
	Class image_class;
	Method method;
	SEL selector;
	char *return_type;

	if (name == NULL || strcmp(name, "Preferences") != 0) {
		return;
	}
	image_class = objc_getClass("UIImage");
	selector = sel_registerName("_applicationIconImageForFormat:precomposed:idiom:scale:");
	method = image_class != NULL ? class_getInstanceMethod(image_class, selector) : NULL;
	if (method == NULL || method_getNumberOfArguments(method) != 6) {
		static const char message[] = "redwood: Preferences: icon workaround unavailable\n";
		say(message, sizeof(message) - 1);
		return;
	}
	return_type = method_copyReturnType(method);
	if (return_type == NULL || strcmp(return_type, "@") != 0) {
		free(return_type);
		return;
	}
	free(return_type);
	class_replaceMethod(image_class, selector, (IMP)preferences_icon_passthrough, method_getTypeEncoding(method));
	{
		static const char message[] = "redwood: Preferences: icon workaround installed\n";
		say(message, sizeof(message) - 1);
	}
}
